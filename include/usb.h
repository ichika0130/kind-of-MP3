#pragma once

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include "display.h"
#include "audio.h"

// ─── UsbManager ───────────────────────────────────────────────────────────────
//
// Handles USB Mass Storage (MSC) mode: when a USB host enumerates the device,
// audio is paused, the SD card is exposed to the host via raw block reads/writes,
// and the display switches to the USB_MSC page.  On disconnect the SD is
// remounted and normal operation resumes.
//
// GPIO19 (D−) and GPIO20 (D+) are the USB bus pins — used exclusively by the
// hardware USB peripheral and must not be assigned to any other function.
//
// Lifetime / init ordering
// ─────────────────────────
// The USBMSC member _msc is instantiated at global-object init time (before
// app_main()), so its constructor runs before USB.begin() in app_main().
// That constructor calls tinyusb_enable_interface(), registering the MSC
// descriptor while the TinyUSB configuration is still being assembled.
// Declaring UsbManager as a global in main.cpp guarantees this timing.

class UsbManager {
public:
    // Constructor: the USBMSC member _msc registers the MSC interface
    // descriptor with TinyUSB at global-init time (before USB.begin()).
    UsbManager();

    // Call once in setup() after power.begin(), before audio.begin().
    // Registers MSC read/write callbacks and sets vendor/product identity.
    void begin();

    // Call every loop() iteration (non-blocking).
    // Polls (bool)USB to detect host connect / disconnect.
    //   On connect : pauses audio, exposes SD via MSC, shows USB_MSC page.
    //   On disconnect : hides SD from host, remounts SD via SPI, resumes audio.
    void update(DisplayState& state, AudioManager& audio);

    // Returns true while USB MSC mode is active.
    // Used by power.cpp to suppress light sleep, and by main.cpp to skip
    // audio.update() while the SD bus is in use by the USB host.
    bool isActive() const { return _usbActive; }

private:
    USBMSC _msc;   // constructed at global init time — triggers TinyUSB registration

    bool _usbActive     = false;
    bool _prevConnected = false;

    // State saved on connect, restored on disconnect
    bool        _wasPlaying = false;
    int         _savedIdx   = 0;
    DisplayPage _savedPage  = DisplayPage::NOW_PLAYING;
    uint32_t    _sectors    = 0;

    // ── TinyUSB MSC callbacks (called from the TinyUSB task) ─────────────────
    // SD.readRAW / writeRAW are called here; the SD FatFS layer remains mounted
    // but is not accessed from the Arduino task while USB MSC is active.
    static int32_t _onRead    (uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static int32_t _onWrite   (uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    static bool    _onStartStop(uint8_t power_condition, bool start, bool load_eject);

    void _enterUsbMode(DisplayState& state, AudioManager& audio);
    void _exitUsbMode (DisplayState& state, AudioManager& audio);
};
