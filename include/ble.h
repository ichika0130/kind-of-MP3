#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "display.h"
#include "audio.h"
#include "sensors.h"
#include "power.h"

// ─── Hardware constants ───────────────────────────────────────────────────────

constexpr uint8_t  VIB_PIN        = 17;      // vibration motor, active HIGH
constexpr uint32_t VIB_MAX_MS     = 2000;    // hard cap on single-pulse duration
constexpr uint32_t BLE_NOTIFY_MS  = 500;     // status notification interval

// ─── BLE identity ─────────────────────────────────────────────────────────────

#define BLE_DEVICE_NAME "P3R-Player"

// ─── UUIDs ────────────────────────────────────────────────────────────────────
//
// All characteristics share the same base UUID family.
// Status (notify+read): 0x02 – 0x0d
// Commands (write):     0x10 – 0x18

#define BLE_SVC_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// Status — notify + read
#define BLE_CHR_TITLE      "4fafc202-1fb5-459e-8fcc-c5c9c331914b"  // UTF-8 string
#define BLE_CHR_POS        "4fafc203-1fb5-459e-8fcc-c5c9c331914b"  // uint32 LE (seconds)
#define BLE_CHR_DUR        "4fafc204-1fb5-459e-8fcc-c5c9c331914b"  // uint32 LE (seconds)
#define BLE_CHR_PLAYING    "4fafc205-1fb5-459e-8fcc-c5c9c331914b"  // uint8: 0=paused 1=playing
#define BLE_CHR_PLAYMODE   "4fafc206-1fb5-459e-8fcc-c5c9c331914b"  // uint8: 0=seq 1=shuffle 2=repeat
#define BLE_CHR_VOLUME     "4fafc207-1fb5-459e-8fcc-c5c9c331914b"  // uint8: 0–21
#define BLE_CHR_BATT       "4fafc208-1fb5-459e-8fcc-c5c9c331914b"  // uint8: 0–100
#define BLE_CHR_STEPS      "4fafc209-1fb5-459e-8fcc-c5c9c331914b"  // uint32 LE
#define BLE_CHR_CALORIES   "4fafc20a-1fb5-459e-8fcc-c5c9c331914b"  // float LE (kcal)
#define BLE_CHR_SPM        "4fafc20b-1fb5-459e-8fcc-c5c9c331914b"  // uint16 LE
#define BLE_CHR_PAGE       "4fafc20c-1fb5-459e-8fcc-c5c9c331914b"  // uint8 DisplayPage
#define BLE_CHR_DARK_HOUR  "4fafc20d-1fb5-459e-8fcc-c5c9c331914b"  // uint8: 0/1

// Commands — write (with or without response)
#define BLE_CMD_PLAYPAUSE  "4fafc210-1fb5-459e-8fcc-c5c9c331914b"  // no payload
#define BLE_CMD_NEXT       "4fafc211-1fb5-459e-8fcc-c5c9c331914b"  // no payload
#define BLE_CMD_PREV       "4fafc212-1fb5-459e-8fcc-c5c9c331914b"  // no payload
#define BLE_CMD_SET_VOL    "4fafc213-1fb5-459e-8fcc-c5c9c331914b"  // 1 byte: 0–21
#define BLE_CMD_SET_MODE   "4fafc214-1fb5-459e-8fcc-c5c9c331914b"  // 1 byte: 0–2
#define BLE_CMD_SET_PAGE   "4fafc215-1fb5-459e-8fcc-c5c9c331914b"  // 1 byte: page index
#define BLE_CMD_SET_TIME   "4fafc216-1fb5-459e-8fcc-c5c9c331914b"  // 3 bytes: [h, m, s]
#define BLE_CMD_SET_WEIGHT "4fafc217-1fb5-459e-8fcc-c5c9c331914b"  // 4 bytes: float LE (kg)
#define BLE_CMD_VIBRATE    "4fafc218-1fb5-459e-8fcc-c5c9c331914b"  // 2 bytes: uint16 LE (ms)

// ─── Internal command type ────────────────────────────────────────────────────

enum class BLECmdType : uint8_t {
    NONE = 0,
    PLAY_PAUSE,
    NEXT,
    PREVIOUS,
    SET_VOLUME,
    SET_PLAY_MODE,
    SET_PAGE,
    SET_BASE_TIME,
    SET_USER_WEIGHT,
    TRIGGER_VIBRATION
};

struct BLECmd {
    BLECmdType type    = BLECmdType::NONE;
    uint8_t    data[8] = {};
    uint8_t    len     = 0;
};

// ─── BLEManager ───────────────────────────────────────────────────────────────

class BLEManager {
public:
    // Call once in setup() after audio.begin().
    // Initialises the BLE stack, creates service and all characteristics,
    // and starts advertising as "P3R-Player".
    bool begin(DisplayState& state);

    // Call every loop() iteration (non-blocking).
    //  • Syncs connection state → state.bleConnected; restarts advertising on disconnect.
    //  • Sends status notifications every BLE_NOTIFY_MS (500 ms) when connected.
    //  • Dequeues and dispatches write commands from the phone.
    //  • Manages the vibration motor timer.
    //  • Detects Dark Hour rising/falling edge and swaps in/out the Tartarus BGM.
    void update(DisplayState& state, AudioManager& audio,
                SensorManager& sensors, PowerManager& power);

    // ── Called by BLE-task callback objects — do not call from user code ──────
    void _enqueueCmd (BLECmdType type, const uint8_t* data, uint8_t len);
    void _setConnected(bool c) { _connected = c; }

private:
    BLEServer*  _server  = nullptr;
    BLEService* _service = nullptr;

    // Status (notify) characteristics
    BLECharacteristic* _cTitle    = nullptr;
    BLECharacteristic* _cPos      = nullptr;
    BLECharacteristic* _cDur      = nullptr;
    BLECharacteristic* _cPlaying  = nullptr;
    BLECharacteristic* _cMode     = nullptr;
    BLECharacteristic* _cVol      = nullptr;
    BLECharacteristic* _cBatt     = nullptr;
    BLECharacteristic* _cSteps    = nullptr;
    BLECharacteristic* _cCal      = nullptr;
    BLECharacteristic* _cSpm      = nullptr;
    BLECharacteristic* _cPage     = nullptr;
    BLECharacteristic* _cDarkHour = nullptr;

    // Set true only when begin() succeeds; update() is a no-op otherwise.
    bool _initialized = false;

    // Connection state — written by BLE task, read by Arduino task
    volatile bool _connected     = false;
    bool          _prevConnected = false;

    // Notify throttle
    unsigned long _lastNotifyMs = 0;

    // Vibration
    bool          _vibActive      = false;
    unsigned long _vibStartMs     = 0;
    uint32_t      _vibDurationMs  = 0;

    // Command queue — written by BLE task, drained by update() on Arduino task
    static constexpr uint8_t QUEUE_SIZE = 8;
    BLECmd           _queue[QUEUE_SIZE];
    volatile uint8_t _qR = 0;   // read pointer
    volatile uint8_t _qW = 0;   // write pointer
    portMUX_TYPE     _mux = portMUX_INITIALIZER_UNLOCKED;  // spinlock

    // Helpers
    BLECharacteristic* _makeNotify(BLEService* svc, const char* uuid);
    void               _makeWrite (BLEService* svc, const char* uuid, BLECmdType t);

    void _sendNotifications(const DisplayState& state);
    void _processCommands  (DisplayState& state, AudioManager& audio,
                            SensorManager& sensors, PowerManager& power);
    void _updateVibration  ();
    void _startVibration   (uint32_t ms);
};
