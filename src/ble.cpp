#include "ble.h"

// ─── BLE-task callback classes ────────────────────────────────────────────────
//
// These run on the ESP32 Bluedroid BLE task — NOT the Arduino main-loop task.
// They must not call AudioManager, SensorManager, or PowerManager directly.
// Instead they set a flag or push a command into the thread-safe queue.

class ConnCallback : public BLEServerCallbacks {
public:
    ConnCallback(BLEManager* mgr) : _mgr(mgr) {}
    void onConnect   (BLEServer*) override { _mgr->_setConnected(true);  }
    void onDisconnect(BLEServer*) override { _mgr->_setConnected(false); }
private:
    BLEManager* _mgr;
};

class CmdCallback : public BLECharacteristicCallbacks {
public:
    CmdCallback(BLEManager* mgr, BLECmdType type) : _mgr(mgr), _type(type) {}
    void onWrite(BLECharacteristic* chr) override {
        String v = chr->getValue();
        _mgr->_enqueueCmd(_type, (const uint8_t*)v.c_str(), (uint8_t)v.length());
    }
private:
    BLEManager* _mgr;
    BLECmdType  _type;
};

// ─── begin ────────────────────────────────────────────────────────────────────

bool BLEManager::begin(DisplayState& /*state*/) {
    // ── Vibration motor ───────────────────────────────────────────────────────
    pinMode(VIB_PIN, OUTPUT);
    digitalWrite(VIB_PIN, LOW);

    // ── BLE stack init ────────────────────────────────────────────────────────
    BLEDevice::init(BLE_DEVICE_NAME);
    _server = BLEDevice::createServer();
    if (!_server) {
        Serial.println("[ble] createServer failed — out of memory?");
        return false;
    }
    _server->setCallbacks(new ConnCallback(this));

    // ── Service ───────────────────────────────────────────────────────────────
    _service = _server->createService(
        BLEUUID(BLE_SVC_UUID),
        /*numHandles=*/60   // 21 chars × 2 handles + 12 CCCDs + service decl
    );
    if (!_service) {
        Serial.println("[ble] createService failed — out of memory?");
        return false;
    }

    // ── Status characteristics (notify + read) ────────────────────────────────
    _cTitle    = _makeNotify(_service, BLE_CHR_TITLE);
    _cPos      = _makeNotify(_service, BLE_CHR_POS);
    _cDur      = _makeNotify(_service, BLE_CHR_DUR);
    _cPlaying  = _makeNotify(_service, BLE_CHR_PLAYING);
    _cMode     = _makeNotify(_service, BLE_CHR_PLAYMODE);
    _cVol      = _makeNotify(_service, BLE_CHR_VOLUME);
    _cBatt     = _makeNotify(_service, BLE_CHR_BATT);
    _cSteps    = _makeNotify(_service, BLE_CHR_STEPS);
    _cCal      = _makeNotify(_service, BLE_CHR_CALORIES);
    _cSpm      = _makeNotify(_service, BLE_CHR_SPM);
    _cPage     = _makeNotify(_service, BLE_CHR_PAGE);
    _cDarkHour = _makeNotify(_service, BLE_CHR_DARK_HOUR);

    // ── Command characteristics (write) ───────────────────────────────────────
    _makeWrite(_service, BLE_CMD_PLAYPAUSE,  BLECmdType::PLAY_PAUSE);
    _makeWrite(_service, BLE_CMD_NEXT,       BLECmdType::NEXT);
    _makeWrite(_service, BLE_CMD_PREV,       BLECmdType::PREVIOUS);
    _makeWrite(_service, BLE_CMD_SET_VOL,    BLECmdType::SET_VOLUME);
    _makeWrite(_service, BLE_CMD_SET_MODE,   BLECmdType::SET_PLAY_MODE);
    _makeWrite(_service, BLE_CMD_SET_PAGE,   BLECmdType::SET_PAGE);
    _makeWrite(_service, BLE_CMD_SET_TIME,   BLECmdType::SET_BASE_TIME);
    _makeWrite(_service, BLE_CMD_SET_WEIGHT, BLECmdType::SET_USER_WEIGHT);
    _makeWrite(_service, BLE_CMD_VIBRATE,    BLECmdType::TRIGGER_VIBRATION);

    _service->start();

    // ── Advertising ───────────────────────────────────────────────────────────
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();

    _initialized = true;
    Serial.printf("[ble] advertising as '%s'\n", BLE_DEVICE_NAME);
    return true;
}

// ─── update ──────────────────────────────────────────────────────────────────

void BLEManager::update(DisplayState& state, AudioManager& audio,
                         SensorManager& sensors, PowerManager& power) {
    if (!_initialized) return;   // begin() failed — nothing to do

    // ── Sync connection state ─────────────────────────────────────────────────
    bool conn = _connected;   // one volatile read
    if (conn != _prevConnected) {
        _prevConnected     = conn;
        state.bleConnected = conn;
        if (conn) {
            Serial.println("[ble] connected");
        } else {
            BLEDevice::startAdvertising();
            Serial.println("[ble] disconnected — advertising restarted");
        }
    }

    // ── Status notifications (every BLE_NOTIFY_MS) ────────────────────────────
    if (conn && millis() - _lastNotifyMs >= BLE_NOTIFY_MS) {
        _lastNotifyMs = millis();
        _sendNotifications(state);
    }

    // ── Queued commands from phone ────────────────────────────────────────────
    _processCommands(state, audio, sensors, power);

    // ── Vibration timer ───────────────────────────────────────────────────────
    _updateVibration();
}

// ─── _sendNotifications ──────────────────────────────────────────────────────

void BLEManager::_sendNotifications(const DisplayState& state) {
    // songTitle — UTF-8 string (char array — no .c_str() needed)
    {
        _cTitle->setValue((uint8_t*)state.songTitle, strlen(state.songTitle));
        _cTitle->notify();
    }

    // songPositionSec — uint32 LE
    {
        uint32_t v = state.songPositionSec;
        _cPos->setValue((uint8_t*)&v, sizeof(v));
        _cPos->notify();
    }

    // songDurationSec — uint32 LE
    {
        uint32_t v = state.songDurationSec;
        _cDur->setValue((uint8_t*)&v, sizeof(v));
        _cDur->notify();
    }

    // isPlaying — uint8
    {
        uint8_t v = state.isPlaying ? 1 : 0;
        _cPlaying->setValue(&v, 1);
        _cPlaying->notify();
    }

    // playMode — uint8
    {
        uint8_t v = static_cast<uint8_t>(state.playMode);
        _cMode->setValue(&v, 1);
        _cMode->notify();
    }

    // volume — uint8
    {
        uint8_t v = state.volume;
        _cVol->setValue(&v, 1);
        _cVol->notify();
    }

    // batteryPct — uint8
    {
        uint8_t v = state.batteryPct;
        _cBatt->setValue(&v, 1);
        _cBatt->notify();
    }

    // stepCount — uint32 LE
    {
        uint32_t v = state.stepCount;
        _cSteps->setValue((uint8_t*)&v, sizeof(v));
        _cSteps->notify();
    }

    // caloriesBurned — float LE (IEEE 754)
    {
        float v = state.caloriesBurned;
        _cCal->setValue((uint8_t*)&v, sizeof(v));
        _cCal->notify();
    }

    // stepsPerMinute — uint16 LE
    {
        uint16_t v = state.stepsPerMinute;
        _cSpm->setValue((uint8_t*)&v, sizeof(v));
        _cSpm->notify();
    }

    // page — uint8
    {
        uint8_t v = static_cast<uint8_t>(state.page);
        _cPage->setValue(&v, 1);
        _cPage->notify();
    }

    // darkHourActive — uint8
    {
        uint8_t v = state.darkHourActive ? 1 : 0;
        _cDarkHour->setValue(&v, 1);
        _cDarkHour->notify();
    }
}

// ─── _processCommands ────────────────────────────────────────────────────────

void BLEManager::_processCommands(DisplayState& state, AudioManager& audio,
                                   SensorManager& sensors, PowerManager& power) {
    while (true) {
        // Dequeue one command under spinlock
        BLECmd cmd;
        portENTER_CRITICAL(&_mux);
        if (_qR == _qW) { portEXIT_CRITICAL(&_mux); break; }
        cmd = _queue[_qR];
        _qR = (_qR + 1) % QUEUE_SIZE;
        portEXIT_CRITICAL(&_mux);

        // Dispatch — runs on Arduino task, safe to call all managers
        switch (cmd.type) {

            case BLECmdType::PLAY_PAUSE:
                if (state.isPlaying) audio.pause(); else audio.resume();
                break;

            case BLECmdType::NEXT:
                audio.next();
                break;

            case BLECmdType::PREVIOUS:
                audio.previous();
                break;

            case BLECmdType::SET_VOLUME:
                if (cmd.len >= 1) {
                    audio.setVolume(cmd.data[0]);
                    state.volume = cmd.data[0];
                }
                break;

            case BLECmdType::SET_PLAY_MODE:
                if (cmd.len >= 1 && cmd.data[0] < 3) {
                    PlayMode m = static_cast<PlayMode>(cmd.data[0]);
                    audio.setPlayMode(m);
                    state.playMode = m;
                }
                break;

            case BLECmdType::SET_PAGE:
                if (cmd.len >= 1 &&
                    cmd.data[0] < static_cast<uint8_t>(DisplayPage::PAGE_COUNT)) {
                    state.page = static_cast<DisplayPage>(cmd.data[0]);
                }
                break;

            case BLECmdType::SET_BASE_TIME:
                if (cmd.len >= 3) {
                    power.setBaseTime(cmd.data[0], cmd.data[1], cmd.data[2]);
                }
                break;

            case BLECmdType::SET_USER_WEIGHT:
                if (cmd.len >= 4) {
                    float kg;
                    memcpy(&kg, cmd.data, 4);
                    if (kg > 0.0f && kg < 500.0f) sensors.setUserWeight(kg);
                }
                break;

            case BLECmdType::TRIGGER_VIBRATION: {
                uint16_t ms = 200;   // default pulse if no payload
                if (cmd.len >= 2) memcpy(&ms, cmd.data, 2);
                else if (cmd.len == 1) ms = cmd.data[0];
                _startVibration(ms);
                break;
            }

            default: break;
        }
    }
}

// ─── _updateVibration / _startVibration ──────────────────────────────────────

void BLEManager::_startVibration(uint32_t ms) {
    uint32_t dur = (ms > VIB_MAX_MS) ? VIB_MAX_MS : ms;
    if (dur == 0) return;
    digitalWrite(VIB_PIN, HIGH);
    _vibActive     = true;
    _vibStartMs    = millis();
    _vibDurationMs = dur;
    Serial.printf("[ble] vibration %lu ms\n", (unsigned long)dur);
}

void BLEManager::_updateVibration() {
    if (!_vibActive) return;
    if (millis() - _vibStartMs >= _vibDurationMs) {
        digitalWrite(VIB_PIN, LOW);
        _vibActive = false;
    }
}

// ─── _enqueueCmd ─────────────────────────────────────────────────────────────
//
// Called from the BLE task.  Uses a spinlock to safely push one entry into the
// circular command queue.  Drops the command silently if the queue is full
// (should not happen at human interaction rates).

void BLEManager::_enqueueCmd(BLECmdType type, const uint8_t* data, uint8_t len) {
    portENTER_CRITICAL(&_mux);
    uint8_t next = (_qW + 1) % QUEUE_SIZE;
    if (next != _qR) {   // not full
        _queue[_qW].type = type;
        _queue[_qW].len  = (len > 8) ? 8 : len;
        memcpy(_queue[_qW].data, data, _queue[_qW].len);
        _qW = next;
    }
    portEXIT_CRITICAL(&_mux);
}

// ─── _makeNotify ─────────────────────────────────────────────────────────────

BLECharacteristic* BLEManager::_makeNotify(BLEService* svc, const char* uuid) {
    BLECharacteristic* c = svc->createCharacteristic(
        uuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    c->addDescriptor(new BLE2902());   // CCCD — lets the client enable notifications
    return c;
}

// ─── _makeWrite ──────────────────────────────────────────────────────────────

void BLEManager::_makeWrite(BLEService* svc, const char* uuid, BLECmdType type) {
    BLECharacteristic* c = svc->createCharacteristic(
        uuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    c->setCallbacks(new CmdCallback(this, type));
}
