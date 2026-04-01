#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "display.h"   // DisplayState
#include "audio.h"     // AudioManager, PlayMode

// ─── Battery ADC constants ────────────────────────────────────────────────────
//
// GPIO7 = ADC1_CH6 on ESP32-S3 (safe with WiFi/BLE, unlike ADC2).
// Adjust BATT_DIVIDER to match your physical voltage-divider resistors:
//   100 kΩ + 100 kΩ → ratio 2.0   (default)
//   100 kΩ + 200 kΩ → ratio 3.0   etc.
// With 11 dB attenuation the ESP32-S3 ADC measures 0 – ~3.1 V.
// A 2:1 divider keeps a 3.5–4.2 V LiPo in the 1.75–2.1 V sweet spot.

constexpr uint8_t  BATT_ADC_PIN      =  7;
constexpr float    BATT_DIVIDER      =  2.0f;
constexpr float    BATT_V_FULL       =  4.20f;  // → 100 %
constexpr float    BATT_V_EMPTY      =  3.50f;  // → 0 %
constexpr uint8_t  BATT_LOW_PCT      = 20;      // lowBattery warning
constexpr uint8_t  BATT_CRITICAL_PCT =  5;      // trigger deep sleep
constexpr uint32_t BATT_INTERVAL_MS  = 30000;   // 30 s between readings

// ─── Sleep constants ──────────────────────────────────────────────────────────

constexpr uint8_t  SLEEP_BTN_PINS[]  = { 1, 2, 3, 14, 15 };  // active-LOW buttons
constexpr uint8_t  SLEEP_MPU_PIN     = 18;                    // MPU6050 INT, active-HIGH
constexpr uint32_t SLEEP_GRACE_MS    = 2000;   // idle time required before light sleep

// ─── Dark Hour constants ──────────────────────────────────────────────────────

constexpr uint32_t DARK_HOUR_DURATION_MS = 60000;  // 60 s at midnight

// ─── NVS namespace ────────────────────────────────────────────────────────────

constexpr char NVS_NAMESPACE[] = "p3rplayer";

// ─── PowerManager ─────────────────────────────────────────────────────────────

class PowerManager {
public:
    // Call in setup() BEFORE audio.begin().
    // Configures the battery ADC and loads any NVS-saved state from a previous
    // deep-sleep cycle.  Does an immediate battery reading so batteryPct is
    // valid from the first display frame.
    bool begin();

    // Call every loop() iteration (non-blocking).
    // Handles: battery sampling (30 s), software RTC, Dark Hour, light sleep,
    // and deep-sleep trigger.
    void update(DisplayState& state, AudioManager& audio);

    // ── Software RTC ──────────────────────────────────────────────────────────
    // Provide current wall-clock time so the RTC can tick from now.
    // Intended to be called by the BLE module once it syncs time with the phone.
    void setBaseTime(uint8_t h, uint8_t m, uint8_t s);

    // ── NVS state restore ─────────────────────────────────────────────────────
    // True when the device woke from deep sleep and valid state was found in NVS.
    // Check in setup() after calling begin() but before other modules start.
    bool hasSavedState() const { return _hasSavedState; }

    // Valid only when hasSavedState() == true.  Apply to AudioManager after
    // audio.begin() completes.
    int      getSavedTrackIdx() const { return _savedTrack;  }
    uint8_t  getSavedVolume()   const { return _savedVolume; }
    PlayMode getSavedPlayMode() const { return static_cast<PlayMode>(_savedMode); }
    // Note: saved position (seconds) is stored for completeness; exact byte-level
    // seek into MP3/FLAC is not yet implemented — playback resumes from track start.

private:
    // ── Battery ───────────────────────────────────────────────────────────────
    uint32_t      _battBuf[10]    = {};  // ring-buffer of raw ADC readings (12-bit)
    uint8_t       _battHead       = 0;
    uint8_t       _battCount      = 0;   // valid entries (0–10)
    unsigned long _lastBattMs     = 0;

    // ── Software RTC ──────────────────────────────────────────────────────────
    unsigned long _baseMs         = 0;   // millis() value when reference time was set
    bool          _rtcSet         = false;

    // ── Dark Hour ─────────────────────────────────────────────────────────────
    bool          _darkActive     = false;
    bool          _darkFiredToday = false;
    unsigned long _darkStartMs    = 0;
    DisplayPage   _savedPage      = DisplayPage::NOW_PLAYING;  // page before dark hour

    // ── Light sleep ───────────────────────────────────────────────────────────
    unsigned long _sleepArmMs     = 0;   // 0 = not armed; >0 = when conditions first met

    // ── NVS saved state ───────────────────────────────────────────────────────
    bool     _hasSavedState = false;
    int      _savedTrack    = 0;
    uint32_t _savedPosSec   = 0;
    uint8_t  _savedVolume   = 10;
    uint8_t  _savedMode     = 0;

    // ── Internal helpers ──────────────────────────────────────────────────────
    void _takeBattSample  (DisplayState& state);
    void _updateRTC       (DisplayState& state);
    void _manageDarkHour  (DisplayState& state, uint32_t secInDay);
    void _manageLightSleep(DisplayState& state, AudioManager& audio);
    void _checkDeepSleep  (DisplayState& state, AudioManager& audio);
    void _saveToNVS       (AudioManager& audio);

    static uint8_t _voltToPct(float v);
};
