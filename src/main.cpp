// ─────────────────────────────────────────────────────────────────────────────
//  main.cpp — P3R wearable MP3 player
//
//  Active modules: display (Step 1) + audio/SD (Step 2) + buttons (Step 3)
//                  + sensors/MPU6050 (Step 4) + power management (Step 5)
//  Pending:        BLE (Step 6)
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include "display.h"
#include "audio.h"
#include "input.h"
#include "sensors.h"
#include "power.h"

// ─── Module instances ─────────────────────────────────────────────────────────

DisplayManager display;
AudioManager   audio;
InputManager   input;
SensorManager  sensors;
PowerManager   power;
DisplayState   state;

// ─── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    // ── Power (first) ─────────────────────────────────────────────────────────
    // Must run before other modules: loads NVS saved state and seeds batteryPct.
    Serial.println("[boot] power...");
    power.begin();
    Serial.println("[boot] power OK");

    // ── Display ───────────────────────────────────────────────────────────────

    Serial.println("[boot] display...");
    if (!display.begin()) {
        Serial.println("[ERROR] SSD1306 not found (SDA=8, SCL=9)");
        pinMode(LED_BUILTIN, OUTPUT);
        while (true) {
            digitalWrite(LED_BUILTIN, HIGH); delay(200);
            digitalWrite(LED_BUILTIN, LOW);  delay(200);
        }
    }
    Serial.println("[boot] display OK");

    // Show a splash while the SD card is scanned
    state.page      = DisplayPage::NOW_PLAYING;
    state.songTitle = power.hasSavedState() ? "Restoring..." : "Loading...";
    state.isPlaying = false;
    display.update(state);

    // ── Audio ─────────────────────────────────────────────────────────────────

    Serial.println("[boot] audio...");
    if (!audio.begin(state)) {
        Serial.println("[WARN] audio init failed — no SD card or no tracks");
        state.songTitle = "No SD card";
    }
    Serial.println("[boot] audio OK");

    // ── Restore audio state from deep-sleep NVS save ──────────────────────────
    // power.begin() loaded the saved values; apply them now that AudioManager is
    // initialised.  Position seek is not yet implemented — playback restarts at
    // the beginning of the saved track.
    if (power.hasSavedState()) {
        audio.setVolume  (power.getSavedVolume());
        audio.setPlayMode(power.getSavedPlayMode());
        audio.play       (power.getSavedTrackIdx());
        Serial.println("[boot] audio state restored from NVS");
    }

    // ── Input ─────────────────────────────────────────────────────────────────

    Serial.println("[boot] input...");
    input.begin();
    Serial.println("[boot] input OK");

    // ── Sensors ───────────────────────────────────────────────────────────────
    // Wire is already up from display.begin() — sensors.begin() uses it directly.

    Serial.println("[boot] sensors...");
    if (!sensors.begin()) {
        Serial.println("[WARN] MPU6050 not found — step count / shake / wake disabled");
    }
    Serial.println("[boot] sensors OK");

    state.page = DisplayPage::NOW_PLAYING;
    Serial.println("[boot] ready");
}

// ─── loop ────────────────────────────────────────────────────────────────────

void loop() {
    // ── 1. Audio engine tick ─────────────────────────────────────────────────
    //    Drives the decoder, polls headphone detect, writes live data to state.
    //    Must run every iteration — no delay() anywhere in the loop.
    audio.update(state);

    // ── 2. Sensor tick ───────────────────────────────────────────────────────
    //    Samples MPU6050 at 50 Hz.  Writes stepCount, caloriesBurned,
    //    stepsPerMinute, screenOn to state; calls display.wake()/sleep() on
    //    screen-state changes.
    sensors.update(state, audio, display);

    // ── 3. Button input ──────────────────────────────────────────────────────
    //    Polls all five buttons; dispatches short/long-press actions directly
    //    against audio and state.  Reads state.isPlaying written by step 1.
    input.update(audio, state);

    // ── 4. Power management ──────────────────────────────────────────────────
    //    Updates batteryPct (every 30 s), ticks software RTC, manages Dark Hour,
    //    enters light sleep when idle, deep-sleeps on critical battery.
    //    May block in esp_light_sleep_start() until a wake source fires.
    power.update(state, audio);

    // ── 5. Volume → contrast sync ────────────────────────────────────────────
    //    Maps volume 0–21 → contrast 40–255 so the display dims with the audio.
    display.setContrast((uint8_t)(40 + (uint16_t)state.volume * 10));

    // ── 6. Render ─────────────────────────────────────────────────────────────
    display.update(state);
}
