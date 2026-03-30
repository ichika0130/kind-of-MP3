#include "sensors.h"
#include <math.h>

// ─── begin ────────────────────────────────────────────────────────────────────
//
// Wire is already initialised by display.begin() with SDA=8, SCL=9.
// We do NOT call Wire.begin() again — just hand the existing bus to the MPU.

bool SensorManager::begin() {
    if (!_mpu.begin()) {
        Serial.println("[sensors] MPU6050 not found (SDA=8, SCL=9, addr=0x68)");
        return false;
    }

    // ── Sensor ranges ─────────────────────────────────────────────────────────
    // ±4g accelerometer range gives 1 mg resolution — enough for step counting.
    // ±500 °/s gyro is well clear of any expected wrist/shake motion.
    _mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    _mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    _mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);  // onboard DLPF, smooths noise

    // ── Motion-detect interrupt (optional hardware assist for pickup) ─────────
    // The interrupt pin (GPIO18) is configured here as an input, but we poll
    // motion via software rather than attaching an ISR, which keeps this module
    // free of ISR / I2C re-entrancy concerns.
    pinMode(MPU_INT_PIN, INPUT);

    // Seed the IIR filters at 1g so step counting is stable from the first sample
    _filtMag   = 9.81f;
    _dynMean   = 9.81f;
    _lastMotionMs = millis();   // start with screen "active"

    Serial.println("[sensors] MPU6050 OK");
    return true;
}

// ─── update ──────────────────────────────────────────────────────────────────

void SensorManager::update(DisplayState& state, AudioManager& audio,
                            DisplayManager& disp) {
    unsigned long now = millis();
    if (now - _lastSampleMs < MPU_SAMPLE_MS) return;
    _lastSampleMs = now;

    // ── Read sensor ───────────────────────────────────────────────────────────
    sensors_event_t a, g, temp;
    _mpu.getEvent(&a, &g, &temp);

    float ax = a.acceleration.x;
    float ay = a.acceleration.y;
    float az = a.acceleration.z;

    // ── Midnight reset (checked once per sample, negligible overhead) ─────────
    _checkMidnight(state);

    // ── All detection happens here ────────────────────────────────────────────
    _processSample(ax, ay, az, state, audio, disp);

    // ── Mirror SPM to DisplayState ────────────────────────────────────────────
    _spm = _calcSPM();
    state.stepsPerMinute = _spm;
}

// ─── _processSample ──────────────────────────────────────────────────────────
//
// Called once per 20 ms.  Does four jobs in a single pass over the sample:
//
//  ① Update IIR-filtered magnitude and dynamic mean.
//  ② Step counting (uses filtered value — tolerant of brief noise spikes).
//  ③ Screen / pick-up management (uses filtered value, same tier analysis).
//  ④ Shake detection (uses RAW magnitude — must catch brief high-g spikes).

void SensorManager::_processSample(float ax, float ay, float az,
                                    DisplayState& state,
                                    AudioManager& audio,
                                    DisplayManager& disp) {
    // ── ① IIR filters ─────────────────────────────────────────────────────────
    //
    // rawMag:   instantaneous vector magnitude in m/s²
    // _filtMag: low-pass smoothed (α=0.85, τ ≈ 120 ms) — catches step peaks
    // _dynMean: very slow moving average (α=0.99, τ ≈ 2 s) — tracks baseline

    float rawMag  = sqrtf(ax*ax + ay*ay + az*az);
    _filtMag  = 0.85f * _filtMag  + 0.15f * rawMag;
    _dynMean  = 0.99f * _dynMean  + 0.01f * rawMag;

    float delta = _filtMag - _dynMean;   // deviation above/below baseline

    // ── ② Step counting ───────────────────────────────────────────────────────
    //
    // Strategy: rising-edge detection on the filtered signal crossing the
    // step threshold above the moving baseline.  A minimum inter-step interval
    // prevents one footfall from producing multiple counts.
    //
    //   [quiet]──────step peak──────[quiet]
    //              ↑ above threshold
    //              └─ one rising edge → one step count

    bool aboveStep = (delta > STEP_THRESHOLD);
    if (aboveStep && !_wasAboveStep) {
        unsigned long now = millis();
        if (now - _lastStepMs >= MIN_STEP_MS) {
            _lastStepMs = now;
            _onStep(state);
        }
    }
    _wasAboveStep = aboveStep;

    // ── ③ Screen / pick-up management ─────────────────────────────────────────
    _updateScreen(delta, disp);
    state.screenOn = _screenOn;   // mirror to DisplayState so PowerManager can gate sleep

    // ── ④ Shake detection (raw magnitude) ────────────────────────────────────
    _checkShake(rawMag, state, audio);
}

// ─── _onStep ─────────────────────────────────────────────────────────────────

void SensorManager::_onStep(DisplayState& state) {
    state.stepCount++;
    state.caloriesBurned = (float)state.stepCount * 0.04f * (_userWeightKg / 70.0f);

    // Push timestamp to SPM circular buffer
    _spBuf[_spHead] = millis();
    _spHead = (_spHead + 1) % SPM_BUF;
    if (_spBufCount < SPM_BUF) _spBufCount++;
}

// ─── _calcSPM ─────────────────────────────────────────────────────────────────
//
// Walk backwards through the circular buffer (newest → oldest).
// Count how many step timestamps fall within the last 10 seconds.
// SPM = count × 6  (steps-per-10s → steps-per-minute).

uint16_t SensorManager::_calcSPM() {
    if (_spBufCount == 0) return 0;

    unsigned long now    = millis();
    constexpr uint32_t WINDOW_MS = 10000;
    uint8_t count = 0;

    for (uint8_t i = 0; i < _spBufCount; i++) {
        // Walk backwards: most recent entry is at (_spHead - 1), etc.
        uint8_t idx = (uint8_t)((_spHead - 1 - i + SPM_BUF) % SPM_BUF);
        if (now - _spBuf[idx] <= WINDOW_MS) {
            count++;
        } else {
            // Buffer is in order: everything further back is also outside window
            break;
        }
    }
    return (uint16_t)(count * 6);
}

// ─── _checkShake ─────────────────────────────────────────────────────────────
//
// A "shake event" is a rising edge on raw magnitude above (dynMean + SHAKE_RAW_THR).
// Three such events within SHAKE_WINDOW_MS → triple-shake → cycle play mode.
//
// Minimum gap between events (MIN_SHAKE_GAP_MS) prevents one sustained high-g
// movement from registering as multiple shakes.

void SensorManager::_checkShake(float rawMag,
                                  DisplayState& state, AudioManager& audio) {
    bool above = (rawMag > _dynMean + SHAKE_RAW_THR);

    if (above && !_inShake) {
        // Rising edge — new shake event
        unsigned long now = millis();
        if (now - _lastShakeEdgeMs >= MIN_SHAKE_GAP_MS) {
            _lastShakeEdgeMs = now;

            // Push event timestamp into the shake ring-buffer
            _shakeTimes[_shakeHead] = now;
            _shakeHead = (_shakeHead + 1) % SHAKE_COUNT_REQ;

            // Check whether all SHAKE_COUNT_REQ events fit within the window.
            // The ring-buffer always holds the last SHAKE_COUNT_REQ timestamps;
            // the oldest is at _shakeHead (the slot we just overwrote wraps back).
            uint8_t oldest = _shakeHead;  // next write slot = oldest stored entry
            unsigned long oldest_t = _shakeTimes[oldest];
            if ((now - oldest_t) <= SHAKE_WINDOW_MS) {
                // Triple shake confirmed
                Serial.println("[sensors] triple-shake → cycle play mode");
                PlayMode next = static_cast<PlayMode>(
                    ((uint8_t)audio.getPlayMode() + 1) % 3);
                audio.setPlayMode(next);
                state.playMode = next;

                // Clear the buffer so the same gesture doesn't re-fire immediately
                memset(_shakeTimes, 0, sizeof(_shakeTimes));
            }
        }
    }
    _inShake = above;
}

// ─── _updateScreen ───────────────────────────────────────────────────────────
//
// Two thresholds operating on the same filtered delta:
//
//   delta > PICKUP_THRESHOLD  →  deliberate movement → wake screen, reset timer
//   delta > REST_THRESHOLD    →  any meaningful motion → reset the sleep timer
//   no significant motion for SCREEN_TIMEOUT_MS  →  sleep screen
//
// This means:
//   - Walking (delta peaks ~1.2–2 m/s²) constantly resets the timer → screen stays on
//   - Sitting still (delta < 0.8 m/s²) → timer expires → screen sleeps
//   - Picking up device (delta > 2.5 m/s²) → screen wakes immediately

void SensorManager::_updateScreen(float delta, DisplayManager& disp) {
    unsigned long now = millis();

    if (delta > PICKUP_THRESHOLD) {
        // Significant motion — wake screen if it was off
        if (!_screenOn) {
            disp.wake();
            _screenOn = true;
            Serial.println("[sensors] screen wake (pick-up)");
        }
        _lastMotionMs = now;
    } else if (delta > REST_THRESHOLD) {
        // Gentle motion (e.g. walking) — just reset the timer, don't wake
        _lastMotionMs = now;
    }

    // Sleep after sustained stillness
    if (_screenOn && (now - _lastMotionMs >= SCREEN_TIMEOUT_MS)) {
        disp.sleep();
        _screenOn = false;
        Serial.println("[sensors] screen sleep (inactivity)");
    }
}

// ─── _checkMidnight ──────────────────────────────────────────────────────────
//
// Resets stepCount and caloriesBurned at midnight.
// state.hour / state.minute are written by the BLE / RTC module (future Step 6).
// Until then they stay 0, so the reset fires once at boot and not again —
// that is harmless.
//
// Guard logic:
//   - Reset fires when hour==0 AND minute==0 AND flag is clear.
//   - Flag is cleared when the clock advances past 00:00 (minute > 0).

void SensorManager::_checkMidnight(DisplayState& state) {
    if (state.hour == 0 && state.minute == 0 && !_midnightResetDone) {
        state.stepCount      = 0;
        state.caloriesBurned = 0.0f;
        _spBufCount          = 0;   // also wipe the SPM buffer
        _spHead              = 0;
        _spm                 = 0;
        _midnightResetDone   = true;
        Serial.println("[sensors] midnight reset: step count cleared");
    }
    // Allow reset to fire again next midnight
    if (state.minute > 0) {
        _midnightResetDone = false;
    }
}
