// ─────────────────────────────────────────────────────────────────────────────
//  main.cpp — P3R wearable MP3 player
//
//  Modules (in init order):
//    Power → USB → Display → Audio → Sensors → Input → BLE
//
//  Loop order:
//    power → usb → audio (skipped in USB MSC mode) → sensors → input
//    → ble → contrast sync → display
//
//  Watchdog: 10-second TWDT on the Arduino loop task.
//  Light sleep: power.update() may block in esp_light_sleep_start(); the WDT
//  is reset immediately on wake (see power.cpp _manageLightSleep).
//  USB MSC: UsbManager is declared as a global so its USBMSC member constructor
//  runs at global-init time, registering the MSC descriptor before USB.begin()
//  assembles the TinyUSB configuration in app_main().
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "display.h"
#include "audio.h"
#include "input.h"
#include "sensors.h"
#include "power.h"
#include "ble.h"
#include "usb.h"

// ─── Module instances ─────────────────────────────────────────────────────────
//
// UsbManager MUST be a global (not stack-allocated in setup()) so that its
// USBMSC member constructor fires before app_main() and USB.begin().

DisplayManager display;
AudioManager   audio;
InputManager   input;
SensorManager  sensors;
PowerManager   power;
BLEManager     ble;
UsbManager     usb;    // global: USBMSC ctor registers MSC descriptor at init time
DisplayState   state;

// ─── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    // ── Watchdog (10 s) ───────────────────────────────────────────────────────
    // esp_task_wdt_init() reconfigures the TWDT if already running (ESP-IDF 4.4),
    // so no deinit is needed.  esp_task_wdt_reset() is called at the top of every
    // loop() iteration and immediately after light-sleep wake (see power.cpp).
    const esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 10000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);   // watch the Arduino loop() task

    // ── Power (first) ─────────────────────────────────────────────────────────
    // Must run before all other modules: loads NVS saved state and seeds
    // batteryPct so the very first display frame shows a valid reading.
    Serial.println("[boot] power...");
    power.begin();
    Serial.println("[boot] power OK");

    // ── USB MSC ───────────────────────────────────────────────────────────────
    // begin() registers read/write callbacks and MSC identity strings.
    // The actual TinyUSB interface was already registered by the USBMSC member
    // constructor at global-init time (before USB.begin() in app_main()).
    Serial.println("[boot] usb...");
    usb.begin();
    Serial.println("[boot] usb OK");

    // ── Display ───────────────────────────────────────────────────────────────
    // Also initialises the I2C bus (Wire, SDA=8, SCL=9) used by the MPU6050.

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
    state.page = DisplayPage::NOW_PLAYING;
    strlcpy(state.songTitle,
            power.hasSavedState() ? "Restoring..." : "Loading...",
            sizeof(state.songTitle));
    state.isPlaying = false;
    display.update(state);

    // ── Audio ─────────────────────────────────────────────────────────────────

    Serial.println("[boot] audio...");
    if (!audio.begin(state)) {
        Serial.println("[WARN] audio init failed — no SD card or no tracks");
        strlcpy(state.songTitle, "No SD card", sizeof(state.songTitle));
        display.update(state);   // immediately show error on OLED
    }
    Serial.println("[boot] audio OK");

    // ── Restore audio state from deep-sleep NVS save ──────────────────────────
    // power.begin() loaded the saved values; apply them now that AudioManager
    // is initialised.  Position seek is not yet implemented — playback restarts
    // at the beginning of the saved track.
    if (power.hasSavedState()) {
        audio.setVolume  (power.getSavedVolume());
        audio.setPlayMode(power.getSavedPlayMode());
        audio.play       (power.getSavedTrackIdx());
        Serial.println("[boot] audio state restored from NVS");
    }

    // ── Sensors ───────────────────────────────────────────────────────────────
    // Wire is already up from display.begin() — sensors.begin() uses it.

    Serial.println("[boot] sensors...");
    if (!sensors.begin()) {
        Serial.println("[WARN] MPU6050 not found — step count / shake / screen-wake disabled");
    }
    Serial.println("[boot] sensors OK");

    // ── Input ─────────────────────────────────────────────────────────────────

    Serial.println("[boot] input...");
    input.begin();
    Serial.println("[boot] input OK");

    // ── BLE ───────────────────────────────────────────────────────────────────
    // Must come after audio.begin() — Dark Hour BGM search needs a populated
    // playlist.

    Serial.println("[boot] BLE...");
    if (!ble.begin(state)) {
        Serial.println("[WARN] BLE init failed — continuing without BLE");
    }
    Serial.println("[boot] BLE OK");

    state.page = DisplayPage::NOW_PLAYING;
    Serial.println("[boot] ready");
}

// ─── loop ────────────────────────────────────────────────────────────────────

void loop() {
    // Reset the watchdog at the top of every iteration.
    // If any module hangs for > 10 s the WDT will panic and reboot.
    esp_task_wdt_reset();

    // ── 1. Power management ──────────────────────────────────────────────────
    //    Updates batteryPct (every 30 s), ticks the software RTC, manages Dark
    //    Hour, and enters light sleep when idle.  Light sleep may block until a
    //    wake source fires; the WDT is reset immediately on wake (power.cpp).
    //    usbMscActive == true prevents light sleep (checked in power.cpp).
    power.update(state, audio);

    // ── 2. USB MSC management ────────────────────────────────────────────────
    //    Detects USB host connect / disconnect.  On connect: pauses audio,
    //    exposes SD to the USB host, and switches to the USB_MSC page.
    //    On disconnect: remounts SD, resumes audio, restores previous page.
    usb.update(state, audio);

    // ── 3. Audio engine tick ─────────────────────────────────────────────────
    //    Skipped while USB MSC is active to prevent concurrent SPI access
    //    between the TinyUSB task (raw block reads/writes) and the audio
    //    decoder (file reads via FatFS).
    if (!usb.isActive()) {
        audio.update(state);
    }

    // ── 4. Sensor tick ───────────────────────────────────────────────────────
    //    Samples MPU6050 at 50 Hz.  Writes stepCount, caloriesBurned,
    //    stepsPerMinute, screenOn to state; calls display.wake()/sleep() on
    //    screen-state transitions.
    sensors.update(state, audio, display);

    // ── 5. Button input ──────────────────────────────────────────────────────
    //    Polls all five buttons; dispatches short/long-press actions against
    //    audio and state.  Reads state.isPlaying written by step 3.
    input.update(audio, state);

    // ── 6. BLE ───────────────────────────────────────────────────────────────
    //    Syncs bleConnected, sends 500 ms status notifications, dispatches
    //    write commands (suppressed in USB MSC mode), manages the vibration
    //    motor, handles Dark Hour BGM.  No-op if BLE init failed.
    ble.update(state, audio, sensors, power);

    // ── 7. Volume → contrast sync ────────────────────────────────────────────
    //    Maps volume 0–21 → contrast 40–255 so the display dims with the audio.
    display.setContrast((uint8_t)(40 + (uint16_t)state.volume * 10));

    // ── 8. Render ─────────────────────────────────────────────────────────────
    display.update(state);
}
