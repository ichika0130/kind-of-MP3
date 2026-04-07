# P3R Player — DIY 可穿戴 MP3 播放器 / DIY Wearable MP3 Player

> 灵感来自《女神异闻录3：重制版》主角的随身听设备
>
> *Inspired by the protagonist's music player from Persona 3 Reload*

<p align="center">
  <img src="docs/preview.jpg" alt="P3R Player" width="300"/>
</p>

---

## 目录 / Table of Contents

- [项目简介 / Overview](#项目简介--overview)
- [硬件 / Hardware](#硬件--hardware)
- [引脚分配 / Pin Assignments](#引脚分配--pin-assignments)
- [功能特性 / Features](#功能特性--features)
- [模块结构 / Module Structure](#模块结构--module-structure)
- [编译与烧录 / Build & Flash](#编译与烧录--build--flash)
- [BLE 配套应用 / BLE Companion App](#ble-配套应用--ble-companion-app)
- [影时间 / Dark Hour Mode](#影时间--dark-hour-mode)
- [灵感与致谢 / Inspiration & Credits](#灵感与致谢--inspiration--credits)

---

## 项目简介 / Overview

**P3R Player** 是一个开源的 DIY 可穿戴音乐播放器项目，外形仿照 *女神异闻录3：重制版* 主角颈挂式随身听（索尼 NW-S203F 造型），以圆柱形吊坠的形式佩戴于颈部。

P3R Player is an open-source DIY wearable music player modeled after the cylindrical pendant worn by the protagonist in *Persona 3 Reload* (based on the Sony NW-S203F form factor). It hangs around the neck like a necklace charm.

- **尺寸 / Dimensions:** ~80 mm × 20 mm（圆柱体 / cylinder）
- **固件平台 / Firmware:** C++ · Arduino framework · PlatformIO
- **主控 / MCU:** ESP32-S3 Super Mini

---

## 硬件 / Hardware

### 元器件清单 / Bill of Materials

| 元器件 / Component | 型号 / Model | 用途 / Purpose |
|---|---|---|
| 主控 MCU | ESP32-S3 Super Mini | 主控制器 / Main controller |
| 显示屏 | 0.91" SSD1306 OLED (128×32) | UI 显示 / UI display |
| 音频 DAC | PCM5102A I2S DAC | 耳机模拟输出 / Headphone analog output |
| 存储卡 | MicroSD 模块 (SPI) | 音乐文件存储 / Music storage |
| IMU | MPU6050 6轴 | 计步 / 摇晃 / 抬腕检测 |
| 振动马达 | 扁平振动电机 3V | 通知振动 / Haptic feedback |
| 耳机接口 | 3.5mm 立体声插孔（含检测） | 耳机输出及自动暂停 |
| 电池 | 500 mAh LiPo | 供电 / Power supply |
| 按键 | 贴片微动开关 ×5 | 操控按键 / Control buttons |

---

## 引脚分配 / Pin Assignments

### I²C 总线 — 显示屏 + MPU6050 / I²C Bus — OLED + MPU6050

| 信号 / Signal | GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |

### I²S 总线 — PCM5102A / I²S Bus — PCM5102A

| 信号 / Signal | GPIO |
|---|---|
| BCLK（位时钟 / Bit Clock） | 4 |
| LRC（帧时钟 / Word Select） | 5 |
| DIN（数据 / Data） | 6 |

### SPI 总线 — MicroSD / SPI Bus — MicroSD

| 信号 / Signal | GPIO |
|---|---|
| MOSI | 11 |
| MISO | 13 |
| SCK | 12 |
| CS（片选 / Chip Select） | 10 |

### 按键 / Buttons（上拉输入，低电平触发 / INPUT_PULLUP, active LOW）

| 功能 / Function | GPIO | 行为 / Behavior |
|---|---|---|
| 翻页 / Page Cycle | 7 | 短按：循环切页（NOW_PLAYING → STEPS → BATTERY）；长按 3 s：进入 BLE 配对模式；配对中短按：退出配对 / Short: cycle pages (NOW_PLAYING → STEPS → BATTERY); Long 3 s: enter BLE pairing; Short while pairing: exit pairing |
| 上一曲 / Previous | 1 | 短按上一曲；长按重头播放 / Short: previous; Long: restart track |
| 下一曲 / Next | 2 | 短按下一曲；长按切换播放模式 / Short: next; Long: cycle play mode |
| 播放/暂停 / Play · Pause | 3 | 短按切换播放/暂停；Clock 页短按：HELLO! 动画 + 恢复播放 / Short: toggle play/pause; On Clock page: HELLO! animation + resume |
| 音量+ / Volume Up | 14 | 短按/长按 +1 步 / Short / long: +1 step |
| 音量- / Volume Down | 15 | 短按/长按 −1 步 / Short / long: −1 step |

### 其他 / Miscellaneous

| 功能 / Function | GPIO | 说明 / Note |
|---|---|---|
| 耳机检测 / Headphone Detect | 16 | 低电平 = 插入 / LOW = inserted |
| 振动马达 / Vibration Motor | 17 | 高电平有效 / Active HIGH |
| MPU6050 中断 / INT | 18 | 高电平有效 / Active HIGH |
| 电池 ADC | 7 | ADC1_CH6，2:1 分压 / voltage divider |

---

## 功能特性 / Features

### 音频播放 / Audio Playback

- 从 MicroSD 卡播放 **MP3 / FLAC** 文件（根目录自动扫描）
- Plays **MP3 / FLAC** files from the MicroSD card root directory (auto-scanned on boot)

- 三种播放模式：顺序 / 随机 / 单曲循环
- Three play modes: **Sequential · Shuffle · Repeat One**

- 拔出耳机自动暂停，插入后自动恢复
- **Auto-pause** on headphone unplug; auto-resume on re-insert

### 显示界面 / Display Pages

设备共有 **7 个显示页面**（5 个用户可切换，通过翻页键短按循环；2 个系统页面）：
The device has **7 display pages** — 5 user-accessible (cycled by Button 1 short press), plus 2 system pages:

| 页面 / Page | 内容 / Content | 进入方式 / Entry |
|---|---|---|
| 🎵 Now Playing | 滚动歌名、进度条、播放/暂停图标、时间 | 翻页键循环 / page cycle |
| 🕐 Clock | 软件 RTC 时钟（由 BLE 同步时间）；睡眠唤醒后自动显示 / auto-shown on wake | 翻页键循环 / page cycle |
| 👟 Steps | 计步数、卡路里消耗、步频（SPM）| 翻页键循环 / page cycle |
| 🔋 Battery | 电池百分比、电量条 | 翻页键循环 / page cycle |
| 🎛️ EQ | 5 频段均衡器，竖排能量条显示各频段增益，顶部显示当前预设名称 / 5-band equalizer with vertical bar display and preset name | 翻页键循环 / page cycle |
| 📶 BLE Pairing | 全屏蓝牙图标，配对状态（PAIR / CONN）；长按翻页键 3 s 进入，60 s 超时或连接后自动退出 / Full-screen Bluetooth symbol; status PAIR or CONN; entered by long-pressing Button 1 for 3 s; auto-exits on connection or 60 s timeout | 长按翻页键 3 s / Long-press Button 1 |
| 🔌 USB MSC | 数据线图标，USB 连接电脑时显示 / Cable icon, shown while USB connected to computer | USB 连接自动触发 / auto on USB connect |

页面切换时使用**右滑入场动画**（200 ms），换曲时使用**下滑动画**（NOW_PLAYING 页）。
Page transitions use a **right-slide-in animation** (200 ms); track changes on the NOW_PLAYING page use a **slide-down animation**.

- 屏幕亮度随音量自动调节 / Screen brightness linked to volume level
- 拿起设备时屏幕自动亮起，静止 5 秒后熄屏 / Screen wakes on pick-up, sleeps after 5 s of inactivity

### 均衡器 / Equalizer (EQ)

- 4 种内置预设：**FLAT / HEAVY / POP / JAZZ**
- 4 built-in presets: **FLAT / HEAVY / POP / JAZZ**

- 通过 BLE 配套应用自定义 5 频段增益（32 Hz / 250 Hz / 1 kHz / 4 kHz / 16 kHz）
- Custom 5-band EQ adjustable via BLE companion app (32 Hz / 250 Hz / 1 kHz / 4 kHz / 16 kHz)

- 设置实时生效，无需重启
- Settings applied in real-time via BLE — no restart required

### 传感器 / Sensors (MPU6050)

- **计步**：IIR 滤波 + 动态基线，精确检测步态峰值
- **Step counting** via IIR low-pass filter and dynamic baseline peak detection

- **卡路里**：步数 × 体重系数（可通过 BLE 设置体重）
- **Calorie estimation** from step count × weight factor (weight configurable via BLE)

- **步频（SPM）**：10 秒滑动窗口内步数 × 6
- **Steps Per Minute (SPM)** from a 10-second rolling window

- **三次摇晃切换随机模式**：1.5 秒内三次高 g 峰值触发
- **Triple-shake** (3 high-g peaks within 1.5 s) toggles Shuffle mode

- **抬腕点亮屏幕**：加速度突变超过阈值即唤醒显示
- **Wrist-raise wake**: acceleration spike above threshold wakes the display

### 省电管理 / Power Management

- **轻度睡眠（Light Sleep）**：屏幕关闭 + 音乐暂停 + 无 BLE 连接时自动进入；按键或 MPU6050 唤醒
- **Light sleep** when screen is off, audio paused, and BLE disconnected; woken by any button or motion interrupt

- **唤醒流程 / Wake sequence**：任意按键或抬腕唤醒 → 显示 **Clock** 页面（5 s）→ 无操作则重新睡眠；在 Clock 页短按 Button 2（播放键）→ HELLO! 动画 → 跳转 NOW_PLAYING 并恢复播放
- **Wake sequence**: any button or wrist-raise → **Clock** page shown for 5 s → re-enters sleep on timeout; pressing Button 2 (Play) on the Clock page → HELLO! animation → NOW_PLAYING with playback resumed

- **深度睡眠（Deep Sleep）**：电量低于 5% 时触发，保存当前曲目/音量/模式至 NVS Flash
- **Deep sleep** triggered at ≤5% battery; current track, volume, and play mode saved to NVS Flash and restored on next boot

- **软件 RTC**：基于 `millis()` 偏移，由 BLE 提供绝对时间
- **Software RTC** based on `millis()` offset, seeded by BLE time sync

- **电池电量检测**：ADC1_CH6，16× 过采样 + 10 样本滚动平均
- **Battery ADC** on GPIO7 with 16× oversampling and 10-sample rolling average

### USB 大容量存储 / USB Mass Storage (MSC)

- 通过 USB-C 连接电脑，SD 卡作为可移动磁盘挂载，可直接拖放音乐文件
- Plug into a computer via USB-C → SD card appears as a removable drive; drag-and-drop music files directly

- USB MSC 激活期间音频自动暂停，BLE 广播停止
- Audio pauses and BLE advertising stops while USB MSC mode is active

- 断开连接后 SD 卡安全重挂载，音频自动恢复播放
- Safe SD remount and audio resume on disconnect

- 连接期间显示专用 **USB MSC** 页面（数据线图标）
- Dedicated **USB MSC** display page (cable icon) shown while connected

### BLE 蓝牙 / BLE

- 设备名称：**`P3R-Player`**，单服务 21 特征值
- Advertises as **`P3R-Player`** with a single GATT service and 21 characteristics

- 状态通知（每 500 ms）：歌名、进度、播放状态、音量、电量、步数、卡路里、步频、页面、影时间标志
- **Status notifications** (every 500 ms): song title, position, duration, play state, volume, battery, steps, calories, SPM, page, Dark Hour flag

- 写入指令：播放/暂停、上/下一曲、设置音量/播放模式/页面/时间/体重、触发振动、设置 EQ 预设、设置自定义 EQ 频段
- **Write commands**: play/pause, next/prev, set volume/mode/page/time/weight, trigger vibration, set EQ preset (uint8_t 0–3), set custom EQ bands (5-byte int8 array, one byte per band)

- **BLE 配对模式 / BLE Pairing Mode**：长按翻页键（Button 1）3 秒进入，显示全屏蓝牙图标页面；60 秒无连接自动退出并恢复原页面；配对成功后自动退出；连接中所有页面均在电池图标旁显示小蓝牙图标（DARK_HOUR 和 WAKE 动画页除外）
- **BLE Pairing Mode**: hold Button 1 (PAGE) for 3 s to enter; shows full-screen Bluetooth icon page; auto-exits after 60 s without a connection, reverting to the previous page; auto-exits on successful pairing; a small Bluetooth icon appears next to the battery indicator on all pages while connected (except DARK_HOUR and WAKE animation)

---

## 模块结构 / Module Structure

所有模块通过共享 `DisplayState` 结构体通信，各模块只写入自己拥有的字段。

All modules communicate through a shared `DisplayState` struct; each module writes only its own fields.

| 文件 / File | 功能 / Responsibility | 拥有的状态字段 / Owned State Fields |
|---|---|---|
| `display.h/cpp` | OLED 渲染，4 页，滚动，亮度 | （只读 / read-only）|
| `audio.h/cpp` | SD 扫描，MP3/FLAC 解码，耳机检测 | `songTitle` `songPositionSec` `songDurationSec` `isPlaying` `playMode` `volume` |
| `input.h/cpp` | 6 键防抖，短按/长按状态机 | `page`（短按翻页循环）|
| `sensors.h/cpp` | MPU6050 50 Hz 采样，计步，摇晃，抬腕 | `stepCount` `caloriesBurned` `stepsPerMinute` `screenOn` |
| `power.h/cpp` | 电池 ADC，软件 RTC，睡眠管理，影时间 | `batteryPct` `lowBattery` `hour` `minute` `second` `darkHourActive` |
| `ble.h/cpp` | BLE 服务器，通知，指令，振动，影时间 BGM | `bleConnected` |

---

## 编译与烧录 / Build & Flash

### 环境依赖 / Prerequisites

- [PlatformIO](https://platformio.org/) （VS Code 插件或 CLI）
- PlatformIO (VS Code extension or CLI)

### 发布构建 / Release Build

```bash
pio run --target upload
```

关闭所有 ESP-IDF 调试输出（`CORE_DEBUG_LEVEL=0`），适合日常使用。

Suppresses all ESP-IDF debug output (`CORE_DEBUG_LEVEL=0`). Use for everyday operation.

### 调试构建 / Debug Build

```bash
pio run -e esp32s3_debug --target upload
```

启用全级别日志（`CORE_DEBUG_LEVEL=5`），串口监视器输出详细信息。

Enables verbose logging (`CORE_DEBUG_LEVEL=5`). Monitor with:

```bash
pio device monitor
```

### USB MSC 注意事项 / USB MSC Note

`platformio.ini` 中的 `-DARDUINO_USB_MODE=0` 标志启用原生 USB OTG 模式，USB MSC 功能需要此标志才能工作。该模式下串口调试输出通过 USB CDC 继续正常工作（无需额外配置）。

The `-DARDUINO_USB_MODE=0` flag in `platformio.ini` enables native USB OTG mode, which is required for USB MSC to function. Serial debug output continues to work via USB CDC in this mode — no additional configuration needed.

### MicroSD 卡要求 / MicroSD Requirements

- 格式：**FAT32**
- 将 MP3 / FLAC 文件放在根目录（`/song.mp3`）
- 影时间 BGM 文件名须包含 `tartarus`（如 `tartarus.mp3`）

- Format: **FAT32**
- Place MP3 / FLAC files directly in the root directory (`/song.mp3`)
- The Dark Hour BGM must have `tartarus` in its filename (e.g., `tartarus.mp3`)

### 库依赖 / Library Dependencies（PlatformIO 自动安装 / auto-installed）

| 库 / Library | 用途 / Purpose |
|---|---|
| `Adafruit SSD1306 ^2.5.9` | OLED 显示驱动 |
| `Adafruit GFX Library ^1.11.9` | 图形渲染 |
| `schreibfaul1/ESP32-audioI2S ^2.0.7` | MP3/FLAC I2S 解码 |
| `Adafruit MPU6050 ^2.2.4` | IMU 驱动 |
| `Adafruit Unified Sensor ^1.1.9` | 传感器抽象层 |
| *(内置 / built-in)* ESP32 BLE | BLE GATT 服务器 |

---

## BLE 配套应用 / BLE Companion App

**开发中 / In Development** — React Native (iOS + Android)

配套手机应用将提供以下功能：

The companion mobile app will provide:

- 远程控制播放（上/下一曲、播放/暂停、音量）
- Remote playback control (next/prev, play/pause, volume)

- 实时歌曲信息与进度条
- Real-time song info and progress bar

- 步数、卡路里、步频统计
- Step count, calorie, and SPM statistics

- 时间同步（为软件 RTC 提供当前时间）
- Time synchronization (seeds the software RTC)

- 触发振动 / 设置体重
- Trigger vibration / set user weight

- 影时间实时通知
- Dark Hour live notification

BLE 服务 UUID：`4fafc201-1fb5-459e-8fcc-c5c9c331914b`

BLE Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

特征值 UUID 规律：`4fafc2XX-...`，其中 `0x02–0x0f` 为状态通知，`0x10–0x1a` 为写入指令。
- `0x0e`：EQ 预设通知（uint8_t）
- `0x0f`：EQ 频段通知（5 × int8_t）
- `0x19`：设置 EQ 预设（写入 1 字节，0–3）
- `0x1a`：设置自定义 EQ 频段（写入 5 字节，每字节对应一个频段增益 dB）

Characteristic UUIDs follow the pattern `4fafc2XX-...`: `0x02–0x0f` are status notify, `0x10–0x1a` are write commands.
- `0x0e`: EQ preset notify (uint8_t)
- `0x0f`: EQ bands notify (5 × int8_t)
- `0x19`: set EQ preset (write 1 byte, 0–3)
- `0x1a`: set custom EQ bands (write 5 bytes, one int8_t gain value per band in dB)

---

## 影时间 / Dark Hour Mode

> *「The Dark Hour... a hidden time that few ever witness.」*

**影时间（Dark Hour）** 是本设备的特色功能，致敬游戏中每天午夜降临的神秘时刻。

**Dark Hour** is the signature feature of this device, a tribute to the hidden hour that appears at midnight in the game.

### 触发条件 / Trigger

当软件 RTC 到达 `00:00:00` 时，影时间自动激活，持续 **60 秒**。触发须先通过 BLE 完成时间同步。

Activates automatically when the software RTC reaches `00:00:00`, lasts **60 seconds**. Requires BLE time sync to have been performed.

### 效果 / Effects

- 显示屏唤醒，页面强制切换至 **DARK_HOUR**，以绿色配色显示大号时间
- Display wakes and force-switches to the **DARK_HOUR** page: large time display with a green colour scheme

- BLE 应用将收到 `darkHourActive = 1` 通知（可用于绿色 UI 等特效）
- BLE companion app receives `darkHourActive = 1` notification (for green UI effects, etc.)

- 60 秒后自动恢复正常状态，同一天内不会重复触发
- Automatically reverts after 60 seconds; fires only once per calendar day

---

## 灵感与致谢 / Inspiration & Credits

本项目的灵感完全来自 **Atlus** 开发、**世嘉（SEGA）** 发行的游戏《**女神异闻录3：重制版（Persona 3 Reload）**》中，主角所佩戴的标志性随身听设备。游戏角色的随身听造型来自索尼 NW-S203F，是《女神异闻录》系列中极具代表性的意象之一。

This project is entirely inspired by the iconic music player worn by the protagonist in ***Persona 3 Reload***, developed by **Atlus** and published by **SEGA**. The in-game device is modeled after the Sony NW-S203F and is one of the most recognizable symbols of the Persona series.

> **⚠️ 免责声明 / Disclaimer**
>
> 本项目为个人爱好制作，与 Atlus、SEGA 及索尼公司无任何隶属或授权关系。
> 《女神异闻录》系列及相关版权归 Atlus/SEGA 所有。本项目仅供个人学习与技术探索，不用于任何商业目的。
>
> This is a personal fan project with no affiliation with or endorsement by Atlus, SEGA, or Sony.
> *Persona* and all related intellectual property are the property of Atlus/SEGA.
> This project is for personal, non-commercial use only.

### 使用的开源库 / Open-Source Libraries Used

| 库 / Library | 作者 / Author | 许可证 / License |
|---|---|---|
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | schreibfaul1 | GPL-3.0 |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | Adafruit Industries | BSD |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | Adafruit Industries | BSD |
| [Adafruit MPU6050](https://github.com/adafruit/Adafruit_MPU6050) | Adafruit Industries | MIT |
| [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) | Espressif Systems | Apache-2.0 |

---

*Made with ♪ and nostalgia.*
