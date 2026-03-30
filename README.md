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
| 音频功放 | MAX98357A I2S DAC/Amp | 音频输出 / Audio output |
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

### I²S 总线 — MAX98357A / I²S Bus — MAX98357A

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

| 功能 / Function | GPIO |
|---|---|
| 上一曲 / Previous | 1 |
| 下一曲 / Next | 2 |
| 播放/暂停 / Play · Pause | 3 |
| 音量+ / Volume Up | 14 |
| 音量- / Volume Down | 15 |

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

设备共有 **4 个显示页面**，通过长按播放键循环切换：
The device has **4 display pages**, cycled by a long-press of the Play button:

| 页面 / Page | 内容 / Content |
|---|---|
| 🎵 Now Playing | 滚动歌名、进度条、播放/暂停图标、时间 |
| 🕐 Clock | 软件 RTC 时钟（由 BLE 同步时间）|
| 👟 Steps | 计步数、卡路里消耗、步频（SPM）|
| 🔋 Battery | 电池百分比、电量条 |

- 屏幕亮度随音量自动调节 / Screen brightness linked to volume level
- 拿起设备时屏幕自动亮起，静止 5 秒后熄屏 / Screen wakes on pick-up, sleeps after 5 s of inactivity

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

- **深度睡眠（Deep Sleep）**：电量低于 5% 时触发，保存当前曲目/音量/模式至 NVS Flash
- **Deep sleep** triggered at ≤5% battery; current track, volume, and play mode saved to NVS Flash and restored on next boot

- **软件 RTC**：基于 `millis()` 偏移，由 BLE 提供绝对时间
- **Software RTC** based on `millis()` offset, seeded by BLE time sync

- **电池电量检测**：ADC1_CH6，16× 过采样 + 10 样本滚动平均
- **Battery ADC** on GPIO7 with 16× oversampling and 10-sample rolling average

### BLE 蓝牙 / BLE

- 设备名称：**`P3R-Player`**，单服务 21 特征值
- Advertises as **`P3R-Player`** with a single GATT service and 21 characteristics

- 状态通知（每 500 ms）：歌名、进度、播放状态、音量、电量、步数、卡路里、步频、页面、影时间标志
- **Status notifications** (every 500 ms): song title, position, duration, play state, volume, battery, steps, calories, SPM, page, Dark Hour flag

- 写入指令：播放/暂停、上/下一曲、设置音量/播放模式/页面/时间/体重、触发振动
- **Write commands**: play/pause, next/prev, set volume/mode/page/time/weight, trigger vibration

---

## 模块结构 / Module Structure

所有模块通过共享 `DisplayState` 结构体通信，各模块只写入自己拥有的字段。

All modules communicate through a shared `DisplayState` struct; each module writes only its own fields.

| 文件 / File | 功能 / Responsibility | 拥有的状态字段 / Owned State Fields |
|---|---|---|
| `display.h/cpp` | OLED 渲染，4 页，滚动，亮度 | （只读 / read-only）|
| `audio.h/cpp` | SD 扫描，MP3/FLAC 解码，耳机检测 | `songTitle` `songPositionSec` `songDurationSec` `isPlaying` `playMode` `volume` |
| `input.h/cpp` | 5 键防抖，短按/长按状态机 | `page`（短按切换）|
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

特征值 UUID 规律：`4fafc2XX-...`，其中 `0x02–0x0d` 为状态通知，`0x10–0x18` 为写入指令。

Characteristic UUIDs follow the pattern `4fafc2XX-...`: `0x02–0x0d` are status notify, `0x10–0x18` are write commands.

---

## 影时间 / Dark Hour Mode

> *「The Dark Hour... a hidden time that few ever witness.」*

**影时间（Dark Hour）** 是本设备的特色功能，致敬游戏中每天午夜降临的神秘时刻。

**Dark Hour** is the signature feature of this device, a tribute to the hidden hour that appears at midnight in the game.

### 触发条件 / Trigger

当软件 RTC 到达 `00:00:00` 时，影时间自动激活，持续 **60 秒**。触发须先通过 BLE 完成时间同步。

Activates automatically when the software RTC reaches `00:00:00`, lasts **60 seconds**. Requires BLE time sync to have been performed.

### 效果 / Effects

- 显示页面强制切换至 Now Playing
- Display force-switches to the Now Playing page

- BLE 应用将收到 `darkHourActive = 1` 通知（可用于绿色 UI 等特效）
- BLE companion app receives `darkHourActive = 1` notification (for green UI effects, etc.)

- 自动在 SD 卡中搜索名称含 `tartarus` 的音乐文件并播放（如 `tartarus.mp3`），结束后恢复之前的曲目
- Automatically searches the SD card for a file containing `tartarus` in the name and plays it; previous track resumes when the hour ends

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
