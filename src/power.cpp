#include "power.h"
#include "esp_sleep.h"      // esp_light_sleep_start, esp_deep_sleep_start, etc.
#include "driver/gpio.h"    // gpio_wakeup_enable, gpio_wakeup_disable

// ─── begin ────────────────────────────────────────────────────────────────────

bool PowerManager::begin() {
    // ── Configure ADC ─────────────────────────────────────────────────────────
    // 11 dB attenuation: effective input range ≈ 0 – 3.1 V on ESP32-S3.
    // GPIO7 is ADC1_CH6 — safe to use alongside WiFi/BLE (ADC2 is not).
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);

    // ── Load NVS saved state ──────────────────────────────────────────────────
    // The `saved` flag is set just before entering deep sleep and cleared here
    // after reading, so a normal power cycle never restores stale data.
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // read-write so we can clear the flag
    _hasSavedState = prefs.getBool("saved", false);

    if (_hasSavedState) {
        _savedTrack  = prefs.getInt  ("track",  0);
        _savedPosSec = prefs.getUInt ("pos",    0);
        _savedVolume = prefs.getUChar("vol",   10);
        _savedMode   = prefs.getUChar("mode",   0);
        prefs.putBool("saved", false);   // consume — won't restore twice
        Serial.printf("[power] NVS restored: track=%d vol=%d mode=%d\n",
                      _savedTrack, _savedVolume, _savedMode);
    }
    prefs.end();

    // ── Initial battery reading ───────────────────────────────────────────────
    // Seed the ring-buffer with 16 reads averaged, so batteryPct is valid
    // from the very first display frame without waiting 30 s.
    uint32_t seed = 0;
    for (uint8_t i = 0; i < 16; i++) seed += analogRead(BATT_ADC_PIN);
    seed /= 16;
    for (uint8_t i = 0; i < 10; i++) _battBuf[i] = seed;
    _battCount = 10;
    _lastBattMs = millis();

    return true;
}

// ─── update ──────────────────────────────────────────────────────────────────

void PowerManager::update(DisplayState& state, AudioManager& audio) {
    // ── Battery sample (every 30 s) ───────────────────────────────────────────
    if (millis() - _lastBattMs >= BATT_INTERVAL_MS) {
        _lastBattMs = millis();
        _takeBattSample(state);
    }

    // ── Software RTC (every loop — cheap arithmetic) ──────────────────────────
    _updateRTC(state);

    // ── Light sleep (every loop — only actually sleeps when conditions met) ───
    _manageLightSleep(state, audio);

    // ── Deep sleep (every loop — triggers if batteryPct ≤ BATT_CRITICAL_PCT) ─
    _checkDeepSleep(state, audio);
}

// ─── setBaseTime ─────────────────────────────────────────────────────────────

void PowerManager::setBaseTime(uint8_t h, uint8_t m, uint8_t s) {
    uint32_t totalSec = (uint32_t)h * 3600u + (uint32_t)m * 60u + s;
    _baseMs  = millis() - totalSec * 1000UL;
    _rtcSet  = true;
    Serial.printf("[power] RTC set to %02d:%02d:%02d\n", h, m, s);
}

// ─── _takeBattSample ─────────────────────────────────────────────────────────
//
// Oversamples the ADC (16×) to reduce ESP32-S3 ADC non-linearity, stores one
// entry in the ring-buffer, then recomputes batteryPct from the full window.

void PowerManager::_takeBattSample(DisplayState& state) {
    // 16× oversample — reduces noise without blocking (each analogRead ≈ 10 µs)
    uint32_t raw = 0;
    for (uint8_t i = 0; i < 16; i++) raw += analogRead(BATT_ADC_PIN);
    raw /= 16;

    // Ring-buffer update
    _battBuf[_battHead] = raw;
    _battHead = (_battHead + 1) % 10;
    if (_battCount < 10) _battCount++;

    // Rolling mean → voltage → percentage
    uint32_t sum = 0;
    for (uint8_t i = 0; i < _battCount; i++) sum += _battBuf[i];
    float rawAvg  = (float)(sum / _battCount);
    float voltage = (rawAvg / 4095.0f) * 3.3f * BATT_DIVIDER;
    uint8_t pct   = _voltToPct(voltage);

    state.batteryPct  = pct;
    state.lowBattery  = (pct <= BATT_LOW_PCT);

    Serial.printf("[power] batt: %.3f V  %d%%\n", voltage, pct);
}

// ─── _voltToPct ──────────────────────────────────────────────────────────────

uint8_t PowerManager::_voltToPct(float v) {
    if (v >= BATT_V_FULL)  return 100;
    if (v <= BATT_V_EMPTY) return 0;
    float pct = (v - BATT_V_EMPTY) / (BATT_V_FULL - BATT_V_EMPTY) * 100.0f;
    return (uint8_t)pct;
}

// ─── _updateRTC ──────────────────────────────────────────────────────────────
//
// Computes the current time from the millis() offset and writes it to state.
// Only active once setBaseTime() has been called.
// millis() rolls over after ~49 days; acceptable for a charged wearable device.

void PowerManager::_updateRTC(DisplayState& state) {
    if (!_rtcSet) return;

    uint32_t elapsed   = (uint32_t)((millis() - _baseMs) / 1000UL);
    uint32_t secInDay  = elapsed % 86400UL;

    state.hour   = (uint8_t)(secInDay / 3600);
    state.minute = (uint8_t)((secInDay % 3600) / 60);
    state.second = (uint8_t)(secInDay % 60);

    _manageDarkHour(state, secInDay);
}

// ─── _manageDarkHour ─────────────────────────────────────────────────────────
//
// Fires once per day in the 2-second window following midnight (secInDay < 2).
// A daily flag prevents re-triggering during that window.
// The flag re-arms at secInDay == 10 so it's ready for the next midnight.
//
// While active: sets state.darkHourActive and forces the NOW_PLAYING page.
// Ends automatically 60 s after activation.

void PowerManager::_manageDarkHour(DisplayState& state, uint32_t secInDay) {
    // ── Start condition ───────────────────────────────────────────────────────
    if (secInDay < 2 && !_darkFiredToday && !_darkActive) {
        _darkActive     = true;
        _darkFiredToday = true;
        _darkStartMs    = millis();
        state.darkHourActive = true;
        state.page = DisplayPage::NOW_PLAYING;
        Serial.println("[power] Dark Hour activated");
    }

    // ── End condition ─────────────────────────────────────────────────────────
    if (_darkActive && (millis() - _darkStartMs >= DARK_HOUR_DURATION_MS)) {
        _darkActive = false;
        state.darkHourActive = false;
        Serial.println("[power] Dark Hour ended");
    }

    // ── Re-arm daily flag ─────────────────────────────────────────────────────
    // Reset at secInDay >= 10 so it's clear well before the next midnight.
    if (secInDay >= 10) {
        _darkFiredToday = false;
    }
}

// ─── _manageLightSleep ───────────────────────────────────────────────────────
//
// Enters ESP32-S3 light sleep when ALL of these hold:
//   • Audio is not playing
//   • BLE is not connected
//   • Screen is off (managed by SensorManager, mirrored to state.screenOn)
//   • All button pins are HIGH (not held at entry)
//   • Conditions have been stable for SLEEP_GRACE_MS
//
// GPIO wake sources:
//   • Buttons (GPIO 1,2,3,14,15): LOW level (active-LOW with INPUT_PULLUP)
//   • MPU6050 INT (GPIO 18):      HIGH level (asserted on motion)
//
// After waking, execution continues from the next line — no reset, no setup().

void PowerManager::_manageLightSleep(DisplayState& state, AudioManager& audio) {
    // ── Guard conditions ──────────────────────────────────────────────────────
    if (state.isPlaying || state.bleConnected || state.screenOn) {
        _sleepArmMs = 0;   // reset grace timer whenever a guard is active
        return;
    }

    // ── Grace period ──────────────────────────────────────────────────────────
    if (_sleepArmMs == 0) {
        _sleepArmMs = millis();
        return;
    }
    if (millis() - _sleepArmMs < SLEEP_GRACE_MS) return;

    // ── Pre-sleep button check ────────────────────────────────────────────────
    // Avoid sleeping if a button is already held (would cause instant wake).
    for (uint8_t pin : SLEEP_BTN_PINS) {
        if (digitalRead(pin) == LOW) {
            _sleepArmMs = millis();   // restart grace period
            return;
        }
    }

    // ── Configure GPIO wake sources ───────────────────────────────────────────
    for (uint8_t pin : SLEEP_BTN_PINS) {
        gpio_wakeup_enable(static_cast<gpio_num_t>(pin), GPIO_INTR_LOW_LEVEL);
    }
    gpio_wakeup_enable(static_cast<gpio_num_t>(SLEEP_MPU_PIN), GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    Serial.println("[power] light sleep ↓");
    Serial.flush();

    esp_light_sleep_start();

    // ── Resumed from light sleep ──────────────────────────────────────────────
    // Disable the GPIO wakeup source so it doesn't interfere with the next cycle.
    for (uint8_t pin : SLEEP_BTN_PINS) {
        gpio_wakeup_disable(static_cast<gpio_num_t>(pin));
    }
    gpio_wakeup_disable(static_cast<gpio_num_t>(SLEEP_MPU_PIN));

    _sleepArmMs = 0;   // require conditions to re-stabilise before next sleep
    Serial.println("[power] light sleep ↑");
}

// ─── _checkDeepSleep ─────────────────────────────────────────────────────────
//
// When batteryPct drops to or below BATT_CRITICAL_PCT:
//  1. Save track index, position, volume, play mode to NVS.
//  2. Configure ext1 wakeup on button pins (LOW level, RTC-capable GPIOs).
//  3. Enter deep sleep.
//
// On the next boot, power.begin() reads NVS and sets hasSavedState() = true.
// main.cpp restores audio state after audio.begin().

void PowerManager::_checkDeepSleep(DisplayState& state, AudioManager& audio) {
    if (state.batteryPct > BATT_CRITICAL_PCT) return;

    Serial.println("[power] critical battery — saving state and deep sleeping");
    Serial.flush();

    _saveToNVS(audio);

    // ext1 wakeup: fires when any RTC GPIO in the mask is LOW.
    // GPIO 1,2,3,14,15 are within GPIO0–GPIO21 on ESP32-S3 → all RTC-capable.
    uint64_t mask = 0;
    for (uint8_t pin : SLEEP_BTN_PINS) mask |= (1ULL << pin);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
    // Never returns.
}

// ─── _saveToNVS ──────────────────────────────────────────────────────────────

void PowerManager::_saveToNVS(AudioManager& audio) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool ("saved", true);
    prefs.putInt  ("track", audio.getCurrentIndex());
    prefs.putUInt ("pos",   audio.getCurrentPositionSec());
    prefs.putUChar("vol",   audio.getVolume());
    prefs.putUChar("mode",  static_cast<uint8_t>(audio.getPlayMode()));
    prefs.end();
    Serial.printf("[power] NVS saved: track=%d pos=%lu vol=%d mode=%d\n",
                  audio.getCurrentIndex(),
                  (unsigned long)audio.getCurrentPositionSec(),
                  audio.getVolume(),
                  (int)audio.getPlayMode());
}
