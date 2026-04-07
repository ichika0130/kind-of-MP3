// ─────────────────────────────────────────────────────────────────────────────
//  usb.cpp — USB Mass Storage (MSC) module
//
//  SD access strategy
//  ──────────────────
//  While USB MSC is active the Arduino task skips audio.update() (enforced in
//  main.cpp), so no firmware code touches the SPI bus.  The SD FatFS layer is
//  kept mounted (SD.end() is NOT called on connect) so that SD.readRAW() /
//  SD.writeRAW() remain available to the TinyUSB callbacks.
//
//  On disconnect: SD.end() + SD.begin() flushes the FatFS cache and picks up
//  any files the host may have written, then audio restarts normally.
//
//  Thread safety
//  ─────────────
//  The _onRead / _onWrite callbacks run on the TinyUSB FreeRTOS task.
//  The SPI mutex inside sd_diskio (AcquireSPI) serialises concurrent access.
//  audio.update() is skipped while _usbActive, so the two paths never overlap.
//
//  USB detection
//  ─────────────
//  On macOS, the project's include/usb.h shadows Arduino's USB.h (same name,
//  case-insensitive filesystem).  We work around this by:
//    1. Not including USB.h directly.
//    2. Forward-declaring the Arduino USB event symbols we need
//       (ARDUINO_USB_EVENTS, arduino_usb_event_handler_register_with), which
//       are defined in cores/esp32/USB.cpp and have C++ linkage.
//    3. Using a volatile flag driven by the event callback for mount detection.
//
//  TinyUSB startup
//  ───────────────
//  With -DARDUINO_USB_MODE=0 and -DARDUINO_USB_CDC_ON_BOOT=1 in platformio.ini,
//  the framework's app_main() calls USB.begin() automatically before setup()
//  runs.  The USBMSC constructor (fired at global-init time, before app_main)
//  has already registered the MSC interface descriptor, so it is included in
//  the TinyUSB configuration when USB.begin() → tinyusb_init() is called.
// ─────────────────────────────────────────────────────────────────────────────

#include "usb.h"      // UsbManager declaration; USBMSC.h included transitively
#include <SD.h>
#include <SPI.h>
#include "esp_event.h" // esp_event_base_t, esp_event_handler_t, ESP_EVENT_ANY_ID

// ─── Arduino USB event forward declarations ───────────────────────────────────
//
// Defined in cores/esp32/USB.cpp.  We cannot #include USB.h because on macOS
// (case-insensitive FS) it resolves to our own include/usb.h instead.

// Event base — defined by ESP_EVENT_DEFINE_BASE(ARDUINO_USB_EVENTS) in USB.cpp
extern const esp_event_base_t ARDUINO_USB_EVENTS;

// Event IDs from the arduino_usb_event_t enum in USB.h
static constexpr int32_t ARDUINO_USB_STARTED = 0;  // ARDUINO_USB_STARTED_EVENT
static constexpr int32_t ARDUINO_USB_STOPPED = 1;  // ARDUINO_USB_STOPPED_EVENT

// C++ free function declared here, defined in USB.cpp
esp_err_t arduino_usb_event_handler_register_with(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg);

// ─── Module-level USB mount state ────────────────────────────────────────────
//
// Written from the Arduino USB event task; read from the Arduino loop task.
// Volatile provides visibility across tasks; no atomics needed for a bool.

static volatile bool s_usbMounted = false;

static void _usbEventCb(void* /*arg*/, esp_event_base_t /*base*/,
                        int32_t event_id, void* /*data*/) {
    if      (event_id == ARDUINO_USB_STARTED) s_usbMounted = true;
    else if (event_id == ARDUINO_USB_STOPPED) s_usbMounted = false;
}

// ─── Constructor ─────────────────────────────────────────────────────────────
//
// The USBMSC member _msc is initialised here at global-object construction time
// (C++ global constructors run before app_main() on ESP-IDF).  Its constructor
// calls tinyusb_enable_interface(), which registers the MSC descriptor before
// app_main() calls USB.begin() → tinyusb_init().

UsbManager::UsbManager() {
    // _msc constructor handles TinyUSB MSC interface registration.
}

// ─── begin ────────────────────────────────────────────────────────────────────
//
// Call once in setup() after audio.begin().
// Registers USBMSC identity strings and callbacks, then subscribes to USB
// mount/unmount events.  TinyUSB was already started by app_main().

void UsbManager::begin() {
    _msc.vendorID("ESP32S3");       // max 8 chars
    _msc.productID("P3R-Player");  // max 16 chars
    _msc.productRevision("1.0");   // max 4 chars
    _msc.isWritable(true);
    _msc.onStartStop(_onStartStop);
    _msc.onRead(_onRead);
    _msc.onWrite(_onWrite);
    _msc.mediaPresent(false);      // no media until USB MSC mode is entered

    // Subscribe to USB mount / unmount events.
    // ESPUSB USB (global in USB.cpp) creates arduino_usb_event_loop_handle in its
    // constructor, which runs before setup(), so the handle is valid here.
    arduino_usb_event_handler_register_with(
        ARDUINO_USB_EVENTS, ESP_EVENT_ANY_ID, _usbEventCb, nullptr);

    Serial.println("[usb] MSC registered (media absent)");
}

// ─── update ──────────────────────────────────────────────────────────────────
//
// Detects USB host mount / unmount via the s_usbMounted flag driven by the
// Arduino USB event callback registered in begin().
//
// This covers three scenarios:
//   • Cable connected before boot:  event fires during app_main(); flag is set
//     before the first update() call (though the race is narrow — USB.begin()
//     → mount typically takes ~500 ms, well after setup() finishes).
//   • Cable connected during use:   flag transitions false → true.
//   • Cable disconnected:           flag transitions true → false.

void UsbManager::update(DisplayState& state, AudioManager& audio) {
    bool conn = s_usbMounted;

    if (conn == _prevConnected) return;
    _prevConnected = conn;

    if (conn && !_usbActive) {
        _enterUsbMode(state, audio);
    } else if (!conn && _usbActive) {
        _exitUsbMode(state, audio);
    }
}

// ─── _enterUsbMode ────────────────────────────────────────────────────────────

void UsbManager::_enterUsbMode(DisplayState& state, AudioManager& audio) {
    Serial.println("[usb] USB host connected — entering MSC mode");

    // Save current state for restoration on disconnect
    _savedPage  = state.page;
    _wasPlaying = state.isPlaying;
    _savedIdx   = audio.getCurrentIndex();

    // Pause audio to close any open SD file handle
    audio.pause();

    // Read sector count while SD FAT is still mounted
    _sectors = SD.numSectors();
    if (_sectors == 0) {
        Serial.println("[usb] SD not available — USB MSC skipped");
        return;
    }

    // Register sector count with TinyUSB MSC and expose media to host
    if (!_msc.begin(_sectors, 512)) {
        Serial.println("[usb] MSC.begin failed (callbacks not registered?)");
        return;
    }
    _msc.mediaPresent(true);

    state.page         = DisplayPage::USB_MSC;
    state.usbMscActive = true;
    _usbActive         = true;
    Serial.printf("[usb] MSC active — %lu sectors × 512 B\n", (unsigned long)_sectors);
}

// ─── _exitUsbMode ─────────────────────────────────────────────────────────────

void UsbManager::_exitUsbMode(DisplayState& state, AudioManager& audio) {
    Serial.println("[usb] USB host disconnected — exiting MSC mode");

    // Hide media from host — any in-flight callbacks are already done
    // (TinyUSB stops when the bus is reset / cable pulled).
    _msc.mediaPresent(false);

    // Remount SD via SPI to flush the FatFS cache and pick up host changes.
    // SPI is already configured with SD pins from audio.begin(); spi.begin()
    // inside SD.begin() is a no-op if already initialised.
    SD.end();
    if (!SD.begin(SD_CS_PIN, SPI, 4000000)) {
        Serial.println("[usb] SD remount failed");
    } else {
        Serial.println("[usb] SD remounted OK");
    }

    state.usbMscActive = false;
    state.page         = _savedPage;
    _usbActive         = false;

    // Restart current track from beginning (byte-level seek not implemented)
    audio.play(_savedIdx);
    if (!_wasPlaying) {
        audio.pause();
    }
    Serial.println("[usb] normal operation resumed");
}

// ─── Static MSC callbacks (TinyUSB task context) ─────────────────────────────
//
// Called by the TinyUSB task in response to SCSI READ10 / WRITE10 commands.
// SD.readRAW / writeRAW map to sd_read_raw / sd_write_raw which acquire the
// SPI mutex internally (AcquireSPI in sd_diskio.cpp), making them task-safe.

int32_t UsbManager::_onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)offset;
    uint8_t* buf = static_cast<uint8_t*>(buffer);
    const uint32_t nSectors = bufsize / 512;
    for (uint32_t i = 0; i < nSectors; i++) {
        if (!SD.readRAW(buf + i * 512, lba + i)) {
            return -1;
        }
    }
    return static_cast<int32_t>(bufsize);
}

int32_t UsbManager::_onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;
    const uint32_t nSectors = bufsize / 512;
    for (uint32_t i = 0; i < nSectors; i++) {
        if (!SD.writeRAW(buffer + i * 512, lba + i)) {
            return -1;
        }
    }
    return static_cast<int32_t>(bufsize);
}

bool UsbManager::_onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;   // always acknowledge; actual media state is managed by mediaPresent()
}
