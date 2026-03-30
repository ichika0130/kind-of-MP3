#pragma once

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "display.h"   // DisplayState, DisplayManager
#include "audio.h"     // AudioManager, PlayMode

// ─── Pin / timing constants ───────────────────────────────────────────────────

constexpr uint8_t  MPU_INT_PIN    = 18;   // MPU6050 interrupt (used for wake detection)
constexpr uint16_t MPU_SAMPLE_MS  = 20;   // 50 Hz — don't sample faster than this

// ─── Detection thresholds (all deltas from the dynamic mean, in m/s²) ─────────
//
//  Magnitude tiers (shared filtered value, one filter — four detection levels):
//
//   REST_THRESHOLD    0.8   "some motion" — resets the screen-sleep timer
//   STEP_THRESHOLD    1.2   "step peak"   — counted as a footstep
//   PICKUP_THRESHOLD  2.5   "deliberate grab/lift" — wakes the screen
//   SHAKE_RAW_THR    10.0   "vigorous shake" — uses RAW magnitude (unfiltered)
//
//  Shake uses raw (not filtered) magnitude so brief spikes are not smoothed away.

constexpr float    STEP_THRESHOLD    =  1.2f;   // m/s²
constexpr float    PICKUP_THRESHOLD  =  2.5f;   // m/s²
constexpr float    REST_THRESHOLD    =  0.8f;   // m/s²
constexpr float    SHAKE_RAW_THR     = 10.0f;   // m/s² above dynamic mean (raw)

constexpr uint16_t MIN_STEP_MS       = 350;     // minimum interval between steps (ms)
constexpr uint32_t SCREEN_TIMEOUT_MS = 5000;    // inactivity before screen sleeps (ms)
constexpr uint16_t SHAKE_WINDOW_MS   = 1500;    // window for triple-shake detection (ms)
constexpr uint16_t MIN_SHAKE_GAP_MS  = 200;     // min time between distinct shake events
constexpr uint8_t  SHAKE_COUNT_REQ   = 3;       // shakes needed to trigger

// ─── SensorManager ────────────────────────────────────────────────────────────

class SensorManager {
public:
    // Wire must already be initialised (display.begin() does this).
    bool begin();

    // Call every Arduino loop iteration (non-blocking).
    // Reads MPU6050 at 50 Hz; all detection and state updates happen here.
    // display.wake() / display.sleep() are called directly when the screen
    // state changes — no flags to poll in main.cpp.
    void update(DisplayState& state, AudioManager& audio, DisplayManager& display);

    // Change the body weight used for calorie estimation (default 70 kg).
    void setUserWeight(float kg) { _userWeightKg = kg; }

    // Current steps-per-minute (BLE module will read this).
    uint16_t getSPM() const { return _spm; }

private:
    Adafruit_MPU6050 _mpu;
    float            _userWeightKg = 70.0f;
    unsigned long    _lastSampleMs = 0;
    uint16_t         _spm          = 0;

    // ── Filtered magnitude (IIR low-pass, τ ≈ 120 ms at 50 Hz) ─────────────
    // Used for step counting, pickup, and rest detection.
    float _filtMag    = 9.81f;   // filtered |acceleration| (m/s²)
    float _dynMean    = 9.81f;   // very-slow-moving baseline (τ ≈ 2 s)

    // ── Step counting ─────────────────────────────────────────────────────────
    bool          _wasAboveStep = false;    // hysteresis flag
    unsigned long _lastStepMs   = 0;

    // ── SPM — circular buffer of the last SPM_BUF step timestamps ────────────
    static constexpr uint8_t SPM_BUF = 30; // covers 3 steps/s × 10 s
    unsigned long _spBuf[SPM_BUF]    = {};
    uint8_t       _spHead            = 0;  // next write slot
    uint8_t       _spBufCount        = 0;  // valid entries (0..SPM_BUF)

    // ── Shake detection ───────────────────────────────────────────────────────
    // Uses raw (unfiltered) magnitude to catch brief spikes.
    bool          _inShake           = false;   // hysteresis: currently above threshold?
    unsigned long _lastShakeEdgeMs   = 0;       // time of last rising-edge event
    unsigned long _shakeTimes[SHAKE_COUNT_REQ] = {};  // ring-buffer of recent shake events
    uint8_t       _shakeHead         = 0;

    // ── Screen / pick-up management ───────────────────────────────────────────
    bool          _screenOn          = true;
    unsigned long _lastMotionMs      = 0;   // last time REST_THRESHOLD was exceeded

    // ── Midnight step/calorie reset ───────────────────────────────────────────
    bool _midnightResetDone = false;

    // ── Internal helpers ──────────────────────────────────────────────────────
    void _processSample(float ax, float ay, float az,
                        DisplayState& state, AudioManager& audio, DisplayManager& disp);
    void _onStep        (DisplayState& state);
    void _checkShake    (float rawMag, DisplayState& state, AudioManager& audio);
    void _updateScreen  (float delta,  DisplayManager& disp);
    void _checkMidnight (DisplayState& state);

    uint16_t _calcSPM();
};
