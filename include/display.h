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
    // Reserved for future use:
    // DARK_HOUR,
    PAGE_COUNT  // keep last — used for cycling
};

// ─── Shared state struct ──────────────────────────────────────────────────────
//
// All display data lives here. Other modules (audio, sensors, power, BLE)
// write into a single global DisplayState; display.cpp only reads from it.
// Nothing is hard-coded inside the display module.

struct DisplayState {
    // ── Active page ──
    DisplayPage page = DisplayPage::NOW_PLAYING;

    // ── NOW_PLAYING ──
    String   songTitle        = "";
    String   artistName       = "";      // reserved — shown in future artist row
    uint32_t songDurationSec  = 0;       // total track length in seconds
    uint32_t songPositionSec  = 0;       // current playback position in seconds
    bool     isPlaying        = false;

    // ── CLOCK ──
    uint8_t  hour    = 0;
    uint8_t  minute  = 0;
    uint8_t  second  = 0;
    String   dateStr = "";   // e.g. "MON 2009/09/14"

    // ── STEPS ──
    uint32_t stepCount       = 0;
    float    caloriesBurned  = 0.0f;

    // ── BATTERY ──
    uint8_t  batteryPct  = 0;    // 0–100
    bool     isCharging  = false;

    // ── AUDIO (written by AudioManager::update) ──
    PlayMode playMode    = PlayMode::SEQUENTIAL;
    uint8_t  volume      = 10;   // 0–21, mirrored from AudioManager

    // ── SENSORS (written by SensorManager::update) ──
    uint16_t stepsPerMinute = 0;   // rolling cadence over last 10 s
    bool     screenOn       = true; // mirrors SensorManager::_screenOn; read by PowerManager

    // ── POWER (written by PowerManager::update) ──
    bool     lowBattery     = false; // true when batteryPct ≤ 20
    bool     darkHourActive = false; // true for 60 s at midnight
    bool     bleConnected   = false; // set by BLE module (Step 6); guards light sleep
};

// ─── Display manager ─────────────────────────────────────────────────────────

class DisplayManager {
public:
    DisplayManager();

    // Call once in setup(). Returns false if the display is not detected.
    bool begin();

    // Call each loop iteration with the latest state. Redraws the whole frame.
    void update(const DisplayState& state);

    // Contrast: 0 = darkest, 255 = brightest.
    // Hook this up to volume level once the audio module is integrated.
    void setContrast(uint8_t contrast);

    // Hardware sleep / wake (display off / on without losing RAM contents)
    void sleep();
    void wake();

private:
    Adafruit_SSD1306 _disp;

    // ── Per-page renderers ──
    void drawNowPlaying(const DisplayState& s);
    void drawClock     (const DisplayState& s);
    void drawSteps     (const DisplayState& s);
    void drawBattery   (const DisplayState& s);

    // ── Scrolling-title state (NOW_PLAYING) ──
    String         _lastTitle         = "";
    int16_t        _scrollX           = 0;    // current left-edge of title text
    unsigned long  _lastScrollTick    = 0;
    int16_t        _scrollHoldTicks   = 0;    // non-zero = paused at start/end

    static constexpr uint16_t SCROLL_TICK_MS  = 40;   // px advance every N ms
    static constexpr uint16_t SCROLL_HOLD_TICKS = 30; // ticks to hold before scrolling

    // ── Helpers ──
    static void    formatSteps(char* buf, size_t len, uint32_t steps);
    static int16_t textWidth  (const char* str, uint8_t size = 1);
};
