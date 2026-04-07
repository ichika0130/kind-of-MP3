# CLAUDE.md — P3R Player Project Context

## Project Overview

P3R-inspired DIY wearable MP3 player modeled after the Sony NW-S203F pendant from *Persona 3 Reload*.

- **MCU:** ESP32-S3 Super Mini
- **Display:** 0.91" SSD1306 OLED (128×32 physical)
- **Audio DAC:** PCM5102A I2S DAC (headphone output)
- **Storage:** MicroSD module (SPI)
- **IMU:** MPU6050 6-axis (step counting, shake, wrist-raise)
- **Firmware:** C++ / Arduino framework / PlatformIO
- **Companion app:** React Native iOS + Android (in development)

---

## Pin Assignments

### I²C Bus — OLED + MPU6050

| Signal | GPIO |
|--------|------|
| SDA    | 8    |
| SCL    | 9    |

### I²S Bus — PCM5102A

| Signal              | GPIO |
|---------------------|------|
| BCLK (Bit Clock)    | 4    |
| LRC  (Word Select)  | 5    |
| DIN  (Data)         | 6    |

### SPI Bus — MicroSD

| Signal | GPIO |
|--------|------|
| MOSI   | 11   |
| MISO   | 13   |
| SCK    | 12   |
| CS     | 10   |

### Buttons (INPUT_PULLUP, active LOW)

| Function       | GPIO | Behavior |
|----------------|------|----------|
| Page Cycle     | 7    | Short: cycle pages (NOW_PLAYING → STEPS → BATTERY → EQ); Long 3 s: enter BLE pairing; Short while pairing: exit pairing |
| Previous       | 1    | Short: previous track; Long: restart track |
| Next           | 2    | Short: next track; Long: cycle play mode |
| Play/Pause     | 3    | Short: toggle play/pause; On Clock page: HELLO! animation + resume |
| Volume Up      | 14   | Short/long: +1 step |
| Volume Down    | 15   | Short/long: −1 step |

### Miscellaneous

| Function            | GPIO | Note |
|---------------------|------|------|
| Headphone Detect    | 16   | LOW = inserted |
| Vibration Motor     | 17   | Active HIGH |
| MPU6050 INT         | 18   | Active HIGH |
| Battery ADC         | 7    | ADC1_CH6, 2:1 voltage divider |

---

## Module Architecture

All modules share a single `DisplayState` struct. Each module writes only its own fields. `display.cpp` is **read-only** on `DisplayState` (exception: `drawWake()` writes `state.page` on animation completion).

| File             | Responsibility                                              | Owned DisplayState Fields |
|------------------|-------------------------------------------------------------|---------------------------|
| `display.h/cpp`  | OLED rendering, page transitions, animations, contrast      | *(read-only)*             |
| `audio.h/cpp`    | SD scan, MP3/FLAC decode via I2S, headphone detect          | `songTitle`, `songPositionSec`, `songDurationSec`, `isPlaying`, `playMode`, `volume`, `eqPreset`, `eqBands` |
| `input.h/cpp`    | 6-button debounce, short/long-press state machine           | `page` (short-press page cycle), `pairingMode`, `prePairingPage` |
| `sensors.h/cpp`  | MPU6050 50 Hz sampling, step count, shake detect, wrist-raise | `stepCount`, `caloriesBurned`, `stepsPerMinute`, `screenOn` |
| `power.h/cpp`    | Battery ADC, software RTC, light/deep sleep, Dark Hour      | `batteryPct`, `lowBattery`, `hour`, `minute`, `second`, `darkHourActive` |
| `ble.h/cpp`      | BLE GATT server, 500 ms notifications, write commands, vibration, Dark Hour BGM swap | `bleConnected` |

**Init order:** Power → Display → Audio → Sensors → Input → BLE

**Loop order:** power → audio → sensors → input → ble → contrast sync → display

---

## Coding Conventions

- **No `delay()` anywhere in `loop()`** — all blocking is forbidden in the main task.
- **All animations use `millis()`-based non-blocking state machines** — track state with `unsigned long` timestamps and index variables.
- **All layout constants as `constexpr` at the top of `display.cpp`** — grouped in the `VLayout` namespace.
- **Loop must complete in < 5 ms per iteration** — the TWDT is set to 10 s but individual frames must not stall.
- **String hot paths use `char[]` not `String`** — `songTitle` is `char[64]`; use `strlcpy()` to write it.
- **BLE callbacks run on the Bluedroid task** — they must not call AudioManager/SensorManager/PowerManager directly; use the thread-safe queue (`_enqueueCmd`).

---

## DisplayPage Enum Values

Defined in `include/display.h`:

```cpp
enum class DisplayPage : uint8_t {
    NOW_PLAYING = 0,
    CLOCK,
    STEPS,
    BATTERY,
    EQ,           // 5-band equaliser; user-accessible via page cycle
    PAGE_COUNT,   // upper bound for BLE SET_PAGE guard (value = 5)
    DARK_HOUR,
    WAKE,
    BLE_PAIRING
};
```

Page cycle order (Button 7 short press): `NOW_PLAYING → STEPS → BATTERY → EQ → NOW_PLAYING`

`CLOCK` and `BLE_PAIRING` are **not** in the cycle — they are entered by system events or long-press.

---

## BLE UUIDs

Device name: `P3R-Player`

**Service:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

### Status Characteristics (notify + read, `0x02`–`0x0f`)

| Characteristic  | UUID                                       | Type / Notes |
|-----------------|--------------------------------------------|--------------|
| Song title      | `4fafc202-1fb5-459e-8fcc-c5c9c331914b`    | UTF-8 string |
| Position        | `4fafc203-1fb5-459e-8fcc-c5c9c331914b`    | uint32 LE (seconds) |
| Duration        | `4fafc204-1fb5-459e-8fcc-c5c9c331914b`    | uint32 LE (seconds) |
| Is playing      | `4fafc205-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0=paused 1=playing |
| Play mode       | `4fafc206-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0=seq 1=shuffle 2=repeat |
| Volume          | `4fafc207-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0–21 |
| Battery %       | `4fafc208-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0–100 |
| Step count      | `4fafc209-1fb5-459e-8fcc-c5c9c331914b`    | uint32 LE |
| Calories        | `4fafc20a-1fb5-459e-8fcc-c5c9c331914b`    | float LE (kcal) |
| SPM             | `4fafc20b-1fb5-459e-8fcc-c5c9c331914b`    | uint16 LE |
| Page            | `4fafc20c-1fb5-459e-8fcc-c5c9c331914b`    | uint8 (DisplayPage index) |
| Dark Hour       | `4fafc20d-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0/1 |
| EQ preset       | `4fafc20e-1fb5-459e-8fcc-c5c9c331914b`    | uint8: 0–3 or 0xFF=custom |
| EQ bands        | `4fafc20f-1fb5-459e-8fcc-c5c9c331914b`    | 5×int8 array (dB gains) |

### Command Characteristics (write, `0x10`–`0x1a`)

| Command         | UUID                                       | Payload |
|-----------------|--------------------------------------------|---------|
| Play/Pause      | `4fafc210-1fb5-459e-8fcc-c5c9c331914b`    | no payload |
| Next            | `4fafc211-1fb5-459e-8fcc-c5c9c331914b`    | no payload |
| Previous        | `4fafc212-1fb5-459e-8fcc-c5c9c331914b`    | no payload |
| Set volume      | `4fafc213-1fb5-459e-8fcc-c5c9c331914b`    | 1 byte: 0–21 |
| Set play mode   | `4fafc214-1fb5-459e-8fcc-c5c9c331914b`    | 1 byte: 0–2 |
| Set page        | `4fafc215-1fb5-459e-8fcc-c5c9c331914b`    | 1 byte: page index (0–PAGE_COUNT-1) |
| Set time        | `4fafc216-1fb5-459e-8fcc-c5c9c331914b`    | 3 bytes: [h, m, s] |
| Set weight      | `4fafc217-1fb5-459e-8fcc-c5c9c331914b`    | 4 bytes: float LE (kg) |
| Vibrate         | `4fafc218-1fb5-459e-8fcc-c5c9c331914b`    | 2 bytes: uint16 LE (ms, max 2000) |
| Set EQ preset   | `4fafc219-1fb5-459e-8fcc-c5c9c331914b`    | 1 byte: 0–3 |
| Set EQ bands    | `4fafc21a-1fb5-459e-8fcc-c5c9c331914b`    | 5 bytes: int8×5 (dB gain per band) |

EQ bands correspond to: 32 Hz / 250 Hz / 1 kHz / 4 kHz / 16 kHz.

---

## Known Constraints

- **ESP32-S3 has no onboard DAC** — all audio output goes through the PCM5102A over I2S (GPIO 4/5/6).
- **SSD1306 rotation:**
  - `WAKE` page → `setRotation(0)` → landscape 128×32
  - All other pages → `setRotation(1)` → portrait 32×128 (logical width=32, height=128)
- **Page cycle order:** `NOW_PLAYING → STEPS → BATTERY → EQ → (back to NOW_PLAYING)` — `CLOCK` and `BLE_PAIRING` are not part of the cycle.
- **`PAGE_COUNT` (value 5)** is not a real page — it is a sentinel used by BLE `SET_PAGE` to guard against out-of-range writes. Do not render it.
- **MicroSD files** must be in the FAT32 root directory. Dark Hour BGM filename must contain `tartarus`.
- **Deep sleep** saves track index, volume, and play mode to NVS; position seek is not implemented (playback restarts from track beginning).
- **Software RTC** is `millis()`-offset only — it drifts and must be re-seeded via BLE `SET_TIME` each session.
- **Watchdog:** 10 s TWDT on the Arduino loop task. `esp_task_wdt_reset()` is called at the top of every `loop()` and immediately after light-sleep wake in `power.cpp`.
- **BLE callbacks run on the Bluedroid task** — never call module methods from them directly; use `_enqueueCmd()` instead.
