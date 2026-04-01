#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── Hardware constants ───────────────────────────────────────────────────────

#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT   32
#define DISPLAY_RESET    -1   // shared reset line; -1 = use Arduino reset
#define DISPLAY_I2C_ADDR 0x3C

#define DISPLAY_SDA_PIN  8
#define DISPLAY_SCL_PIN  9

// ─── Shared enums (used by display + audio) ──────────────────────────────────

// Defined here so every module that includes display.h gets PlayMode for free,
// without creating a dependency on audio.h.
enum class PlayMode : uint8_t {
    SEQUENTIAL = 0,
    SHUFFLE,
    REPEAT_ONE
};

// ─── Display pages ────────────────────────────────────────────────────────────

enum class DisplayPage : uint8_t {
    NOW_PLAYING = 0,
    CLOCK,
    STEPS,
    BATTERY,
    PAGE_COUNT,   // upper bound for user-accessible page cycling (keep at 4)
    DARK_HOUR,    // not user-accessible; entered automatically at midnight
    WAKE          // wake animation; horizontal orientation, not user-accessible
};

// ─── Shared state struct ──────────────────────────────────────────────────────
//
// Single source of truth for all display data.  Modules write their own fields;
// display.cpp only reads.  Field ownership (primary writer in parentheses):
//
//  Field(s)                         Owner          Notes
//  ──────────────────────────────── ────────────── ──────────────────────────────
//  page                             InputManager   PowerManager also writes on
//                                                  Dark Hour; BLEManager on
//                                                  SET_PAGE cmd (shared control)
//  songTitle                        AudioManager   char buf — no heap alloc
//  artistName                       AudioManager   reserved for future use
//  songDurationSec, songPositionSec AudioManager
//  isPlaying                        AudioManager
//  playMode                         AudioManager   InputManager / SensorManager /
//                                                  BLEManager write for immediate
//                                                  feedback; audio.update() is
//                                                  authoritative each iteration
//  volume                           AudioManager   BLEManager writes for immediate
//                                                  feedback; audio.update() is
//                                                  authoritative
//  hour, minute, second             PowerManager   set once via BLE setBaseTime
//  dateStr                          (unset)        will be written by BLE time sync
//  stepCount, caloriesBurned        SensorManager
//  batteryPct, lowBattery           PowerManager
//  darkHourActive                   PowerManager
//  isCharging                       (unset)        no charging-detect circuit yet
//  stepsPerMinute, screenOn         SensorManager
//  bleConnected                     BLEManager

struct DisplayState {
    // ── Active page ──
    DisplayPage page = DisplayPage::NOW_PLAYING;

    // ── NOW_PLAYING ──
    char     songTitle[64]    = {};      // char array — avoids heap alloc in loop
    String   artistName       = "";      // reserved — shown in future artist row
    uint32_t songDurationSec  = 0;       // total track length in seconds
    uint32_t songPositionSec  = 0;       // current playback position in seconds
    bool     isPlaying        = false;

    // ── CLOCK ──
    uint8_t  hour    = 0;
    uint8_t  minute  = 0;
    uint8_t  second  = 0;
    String   dateStr = "";   // e.g. "MON 2009/09/14" — set by BLE time sync

    // ── STEPS ──
    uint32_t stepCount       = 0;
    float    caloriesBurned  = 0.0f;

    // ── BATTERY ──
    uint8_t  batteryPct  = 0;    // 0–100
    bool     isCharging  = false;  // no charging-detect circuit; always false for now

    // ── AUDIO (written by AudioManager::update) ──
    PlayMode playMode    = PlayMode::SEQUENTIAL;
    uint8_t  volume      = 10;   // 0–21, mirrored from AudioManager

    // ── SENSORS (written by SensorManager::update) ──
    uint16_t stepsPerMinute = 0;   // rolling cadence over last 10 s
    bool     screenOn       = true; // mirrors SensorManager::_screenOn; read by PowerManager

    // ── POWER (written by PowerManager::update) ──
    bool     lowBattery     = false; // true when batteryPct ≤ 20
    bool     darkHourActive = false; // true for 60 s at midnight
    bool     bleConnected   = false; // set by BLE module; guards light sleep
};

// ─── Display manager ─────────────────────────────────────────────────────────

class DisplayManager {
public:
    DisplayManager();

    // Call once in setup(). Returns false if the display is not detected.
    bool begin();

    // Call each loop iteration with the latest state. Redraws the whole frame.
    // Non-const: WAKE animation writes state.page = NOW_PLAYING on completion.
    void update(DisplayState& state);

    // Contrast: 0 = darkest, 255 = brightest.
    // Hook this up to volume level once the audio module is integrated.
    void setContrast(uint8_t contrast);

    // Hardware sleep / wake (display off / on without losing RAM contents)
    void sleep();
    void wake();

private:
    Adafruit_SSD1306 _disp;
    int              _currentRotation = 0;   // tracks last setRotation() call; avoids redundant I2C commands

    // ── Per-page renderers ──
    void drawNowPlaying(const DisplayState& s);
    void drawClock     (const DisplayState& s);
    void drawSteps     (const DisplayState& s);
    void drawBattery   (const DisplayState& s);
    void drawDarkHour  (const DisplayState& s);
    void drawWake      (DisplayState& s);            // writes s.page on completion

    // ── Scrolling-title state (NOW_PLAYING) ──
    char           _lastTitle[64]     = {};   // char array matches DisplayState::songTitle
    int16_t        _scrollX           = 0;    // current left-edge of title text
    unsigned long  _lastScrollTick    = 0;
    int16_t        _scrollHoldTicks   = 0;    // non-zero = paused at start/end

    // ── Dark Hour animation state ──
    unsigned long  _darkHourEnteredMs = 0;    // millis() when DARK_HOUR page was entered; 0 = not entered

    // ── WAKE animation state ──
    int            _wakeAnimState     = 0;    // 0=revealing chars, 1=holding; reset on re-entry
    int            _wakeCharIndex     = 0;    // chars of "HELLO!" revealed so far (0–6)
    unsigned long  _wakeAnimTick      = 0;    // last state-transition timestamp; 0 = uninitialised

    // ── Track-change slide animation (NOW_PLAYING) ──
    int16_t        _slideOffset       = 0;    // Y offset applied to all NOW_PLAYING elements
    bool           _sliding           = false;
    unsigned long  _slideStartMs      = 0;

    // ── Page transition tracking ──
    DisplayPage    _prevPage          = DisplayPage::NOW_PLAYING;

    static constexpr uint16_t SCROLL_TICK_MS   = 40;   // ms between 1-px scroll advances
    static constexpr uint16_t SCROLL_HOLD_TICKS = 30;  // ticks to hold before scrolling

    // ── Helpers ──
    static void    formatSteps(char* buf, size_t len, uint32_t steps);
    static int16_t textWidth  (const char* str, uint8_t size = 1);
};
