# PCDash — 桌面PC实时监控屏幕 产品需求文档 (PRD)

> **版本:** v1.5
> **日期:** 2026-06-23
> **状态:** 迭代更新
> **变更摘要:** WiFi 重连死锁 bug 修复（`wifiConnected` 标志恢复 + 周期性重连）；DHT11 关中断保护 + 失败重试 + 温度修正 -2°C；背光控制移除（GPIO0 strapping pin，硬件无调光电路）；新增 `STATUS` 串口命令打印系统全状态；HTTP 端口改为每槽位独立（`pc_ports[]`）；串口命令 `PORT:` 格式更新为 `PORT:index,port`；诊断日志增强（WiFi/设备状态定期输出）。

---

## 目录

1. [产品概述](#1-产品概述)
2. [系统架构](#2-系统架构)
3. [硬件设计](#3-硬件设计)
4. [功能需求](#4-功能需求)
5. [通信协议设计](#5-通信协议设计)
6. [ESP 端软件架构](#6-esp-端软件架构)
7. [PC 端软件架构](#7-pc-端软件架构)
8. [配置管理](#8-配置管理)
9. [容错与异常处理](#9-容错与异常处理)
10. [OTA 与后续扩展](#10-ota-与后续扩展)
11. [开发里程碑](#11-开发里程碑)
12. [附录](#附录)

---

## 1. 产品概述

### 1.1 产品定位

PCDash 是一款桌面PC实时硬件监控屏幕。由一块基于 ESP32-S3 MCU 驱动的 TFT 彩色显示屏作为显示终端，通过 WiFi 或 USB 串口与 PC 端 Python 服务通信，实时展示 CPU、内存、网络、GPU 等核心硬件指标，同时显示实时时钟、农历信息、天气（含风向风力）。

### 1.2 核心特性

- **双通道通信:** WiFi 和 USB 串口可同时工作，互不干扰，串口数据优先
- **LVGL v9.3 UI 框架:** 基于 LVGL + SquareLine Studio 1.6.1 构建声明式 UI，替代原 TFT_eSPI + Sprite 直接绘制方案
- **单页双面板布局:** 上半区 CPU + GPU 双栏面板，下半区时钟 + 天气 + 农历，触摸滑动切换多台设备
- **动态进度条颜色:** 进度条前景色按阈值实时变绿/黄/红，直观反映负载和温度状态
- **CPU/GPU 型号显示:** 标题栏显示实际 CPU/GPU 型号（如 "i7-12700KF"、"RTX 3060"），替代原静态文字
- **天气四控件拆分:** 城市、描述、温度（Montserrat 字体，无中文字符，避免 U+B0 glyph 警告）、湿度各自独立；新增风向风力控件
- **NTP 优先 + 串口回退:** WiFi 环境下 NTP 独立授时；无 WiFi 时自动降级为 PC 串口推送的系统时间
- **多 PC 监控:** WiFi 模式下可同时连接多台 PC，触摸滑动切换显示
- **DHT11 室内温湿度:** GPIO37（P8 跳帽接入），10 秒采样一次，实时显示室温/室湿；精度 ±2°C / ±5%RH；关中断保护 bit-bang 时序 + 失败自动重试 3 次 + 温度修正 -2°C
- **PC 端跨平台:** Python 实现，Windows 下任务栏托盘运行，Linux 下 systemd 服务运行
- **农历完整信息:** PC 端离线计算干支年月日、农历月日、节气，无需 API

### 1.3 目标用户

关注硬件状态的开发者、游戏玩家、服务器运维人员，希望在桌面上放置一块独立小屏幕，无需切换窗口即可实时监控 PC 运行状态。

---

## 2. 系统架构

### 2.1 架构总览

```
┌─────────────────────┐     WiFi (HTTP GET)     ┌────────────────────────┐
│  PC-A Python Service │ ◄───────────────────────┤                        │
└─────────────────────┘                          │   ESP32-S3             │
┌─────────────────────┐     WiFi (HTTP GET)      │   ILI9341 320×240      │
│  PC-B Python Service │ ◄───────────────────────┤   LVGL v9.3 UI         │
└─────────────────────┘                          │   触摸屏（XPT2046）    │
┌─────────────────────┐     USB Serial (Push)    │   DHT11 温湿度传感器   │
│  PC-C Python Service │ ────────────────────────►│                        │
└─────────────────────┘                          └────────────────────────┘
```

### 2.2 通信模式对比

| 特性 | WiFi (HTTP) | USB 串口 |
|------|-------------|----------|
| 通信方向 | ESP 主动轮询 PC 的 HTTP 接口 | PC 主动推送数据到 ESP |
| 多 PC 支持 | 支持，ESP 轮询多个 IP | 仅支持单台（点对点） |
| 依赖 | 局域网 WiFi、独立供电 | USB 线（供电+数据一体） |
| 延迟 | 200-500ms（HTTP 开销） | < 10ms（直连） |
| 稳定性 | 受 WiFi 信号影响 | 极高（有线直连） |
| 时间同步 | NTP 自主授时（每 30 分钟同步） | 使用 PC 下发 time 字段 |

两种通道可同时工作。当两个通道同时收到同一台 PC 的数据时，USB 串口数据优先。

---

## 3. 硬件设计

### 3.1 显示屏模块

| 参数 | 规格 |
|------|------|
| 屏幕尺寸 | 2.8 寸 TFT |
| 分辨率 | 320 × 240 (RGB565) |
| 横屏使用 | **320 × 240** (`setRotation(3)`) |
| 驱动芯片 | ILI9341 |
| 通信接口 | SPI |
| 背光控制 | GPIO0 连接 LED 引脚，硬件无调光电路，始终常亮 |
| 触摸屏 | XPT2046，SPI |

### 3.2 GPIO 分配 (ESP32-S3)

| GPIO | 功能 |
|------|------|
| 0 | TFT 背光常亮（strapping pin，无调光电路） |
| 1 | 触摸屏 CS (TOUCH_CS_PIN) |
| 2 | 触摸屏 DIN (TOUCH_DIN_PIN) |
| 41 | 触摸屏 DO (TOUCH_DO_PIN) |
| 42 | 触摸屏 CLK (TOUCH_CLK_PIN) |
| 37 | DHT11 数据引脚（P8 跳帽，DHT11_PIN） |
| 38 | CC 状态 LED 红（CC_LED_RED_PIN） |
| 39 | CC 状态 LED 黄（CC_LED_YLW_PIN） |
| 40 | CC 状态 LED 绿（CC_LED_GRN_PIN） |

> **注：** 板上不存在物理按键，K1/K2/K3 定义已移除，GPIO35/36/37 已释放；GPIO37 通过 P8 跳帽接入 DHT11，不使用时拔掉跳帽可恢复空闲。

> **PSRAM 注意事项:** ESP32-S3 N16R8 内置 8MB OPI PSRAM，但 Arduino ESP32 Core 3.x 对本硬件的 OPI PSRAM 初始化存在兼容性问题（RTCWDT_RTC_RST 崩溃）。当前项目维持 **PSRAM: Disabled**，320×240 LVGL 局部缓冲刷新在内部 512KB SRAM 内完全可行。

### 3.3 MCU 平台

当前主平台：**ESP32-S3**（原生 USB-OTG CDC，无需外置 USB-UART 芯片）。

---

## 4. 功能需求

### 4.1 监控数据项

| 数据项 | 数据源 | 刷新频率 | 说明 |
|--------|--------|----------|------|
| CPU 使用率 | `psutil.cpu_percent()` | 2-3秒 | 百分比，精度 0.1% |
| CPU 型号 | Windows: winreg；Linux: /proc/cpuinfo | 启动时读取一次并缓存 | 如 "i7-12700KF"，最长 31 字节 |
| CPU 温度 | Windows: LHM Web Server；Linux: psutil | 2-3秒 | °C；不可用时为 null |
| CPU 主频 | `psutil.cpu_freq()` | 2-3秒 | MHz；不可用时为 null |
| CPU 功率 | Windows: LHM；Linux: Intel RAPL | 2-3秒 | W；不支持时为 null |
| 内存使用率 | `psutil.virtual_memory()` | 2-3秒 | 百分比 + 已用/总量(GB) |
| 网络上传/下载速度 | `psutil.net_io_counters()` 差值 | 2-3秒 | bytes/s，显示时换算 KB/MB |
| GPU 使用率 | pynvml (NVML) | 2-3秒 | 无 NVIDIA GPU 时为 null |
| GPU 温度 | pynvml (NVML) | 2-3秒 | °C |
| GPU 显存使用率 | pynvml (NVML) | 2-3秒 | 百分比 |
| GPU 型号 | pynvml `nvmlDeviceGetName()` | 启动时读取 | 字符串，最长 24 字节 |
| GPU 实时功率 | pynvml `nvmlDeviceGetPowerUsage()` | 2-3秒 | W；传感器不可用时为 null |
| 实时时间 | 优先 NTP（WiFi）；回退 PC 串口推送 time 字段 | 每秒本地刷新 / 30分钟 NTP 同步 | HH:MM |
| 实时日期 | 同上 | 同上 | YYYY-MM-DD |
| 农历日期 | `sxtwl` 离线计算 | 每次推送携带 | 干支年月日 + 农历月日 + 节气 |
| 天气描述 | Open-Meteo / OpenWeatherMap / 和风天气 | 30 分钟 | 天气文字（如"晴"） |
| 天气温度 | 同上 | 30 分钟 | °C（Montserrat 字体显示） |
| 天气湿度 | 同上 | 30 分钟 | %（Montserrat 字体显示） |
| 风向风力 | 同上 | 30 分钟 | 如"东北风 3级"（拼接字符串） |
| 城市名 | 同上 / IP 自动定位 | 定位后固定 | UTF-8 中文 |
| **室内温度** | DHT11 本地传感器（GPIO37） | 10 秒 | °C；整数精度 ±2°C |
| **室内湿度** | DHT11 本地传感器（GPIO37） | 10 秒 | %RH；整数精度 ±5%RH |

### 4.2 UI 布局（单页双面板）

```
┌──────────────────────────────────────────────────────┐
│  状态栏 (ui_pnlstatusbar)                             │
│  [↑上传] [↓下载]     [设备名]    [连接类型]          │
├──────────────────────┬───────────────────────────────┤
│  CPU 面板            │  GPU 面板                      │
│  [CPU型号]           │  [GPU型号]                     │
│  USG ████░░ 45%      │  USG ████░░ 72%                │
│  TMP ███░░░ 62°C     │  TMP ███░░░ 68°C               │
│  PWR ██░░░░ 95W      │  PWR █████░ 220W               │
│  MEM ███░░░ 51%      │  MEM ██░░░░ 42%                │
├──────────────────────┴───────────────────────────────┤
│  下半区 (ui_pnlclock)                                 │
│  14:30        2026-06-14                              │
│  [城市] [天气描述] [28°C] [65%] [东北风 3级]          │
│                          室内:[25°C]  [48%]           │
│  四月廿五   甲辰年              芒种                   │
│             ● ○ ○                                     │
└──────────────────────────────────────────────────────┘
```

### 4.3 进度条动态颜色规则

| Bar 类型 | 绿色（正常） | 黄色（警告） | 红色（危险） |
|----------|------------|------------|------------|
| CPU/GPU 使用率 % | ≤ 60% | 61–90% | > 90% |
| 内存/显存占用 % | ≤ 60% | 61–90% | > 90% |
| CPU/GPU 功率预算 % | ≤ 60% | 61–90% | > 90% |
| CPU 温度 (°C) | ≤ 70°C | 71–90°C | > 90°C |
| GPU 温度 (°C) | ≤ 70°C | 71–85°C | > 85°C |

实现方式：每次数据刷新时调用 `set_bar_color(bar, val, warn, crit)`，通过 `lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR)` 动态设置进度条前景色。

### 4.4 交互方式

板上不存在物理按键，设备切换通过触摸滑动手势实现：

| 手势 | 功能 |
|------|------|
| 左滑 | 下一台设备 |
| 右滑 | 上一台设备 |
| 自动 | `AUTO_SCROLL_MS`（3秒）无操作后自动轮换 |

---

## 5. 通信协议设计

### 5.1 数据帧格式 (JSON)

WiFi 和 USB 串口使用统一的 JSON 数据格式：

```json
{
  "id":       "My-PC",
  "cpu":      45.2,
  "cpu_name": "i7-12700KF",
  "cpu_t":    65,
  "cpu_f":    3600,
  "cpu_w":    95,
  "mem":      { "pct": 51.2, "used": 16.4, "total": 32.0 },
  "net":      { "up": 14832, "dn": 5007 },
  "gpu":      { "usage": 78, "temp": 65, "mem_pct": 42, "name": "RTX 3060", "watt": 120 },
  "time":     "14:30:25",
  "date":     "2026-06-14",
  "bgHash":   "a1b2c3d4",
  "lun":      { "gz_y": "丙午年", "gz_m": "壬午月", "gz_d": "甲子日",
                "mo": "四月", "dy": "廿五", "jq": "芒种" },
  "wth":      { "temp": 28, "humidity": 65, "icon": "02d",
                "desc": "晴", "city": "北京", "wind": "东北风 3级" }
}
```

### 5.2 字段说明（v1.3 新增/变更字段）

| 字段 | 类型 | 说明 | 变更 |
|------|------|------|------|
| cpu_name | string | CPU 型号，最长 31 字节，启动时采集缓存 | **新增** |
| bgHash | string | 背景图 MD5 前 8 位；空串表示无背景 | **新增** |
| wth.humidity | int | 实时相对湿度 (%) | **新增** |
| wth.wind | string | 风向+风力，如"东北风 3级"，最长 23 字节 | **新增** |
| wth.desc | string | 天气描述文字（可为中文，如"晴"） | v1.2 为英文，v1.3 支持中文 |

完整字段说明见 v1.2 §5.2，以上为增量变更。

### 5.3 风向风力拼接规则

| 天气渠道 | 风向来源 | 风力来源 | 拼接格式 |
|---------|---------|---------|---------|
| OpenWeatherMap | `wind.deg`（角度）→ 8方位转换 | `wind.speed`（m/s）→ 蒲福级 | "东北风 3级" |
| Open-Meteo | `wind_direction_10m`（角度）→ 8方位 | `wind_speed_10m`（km/h）÷3.6→ 蒲福级 | "东北风 3级" |
| 和风天气 | `now.windDir`（中文，直接使用） | `now.windScale`（级数，直接使用） | "东北风 3级" |

角度→方位（8方位，每 45°）：北/东北/东/东南/南/西南/西/西北。

### 5.4 WiFi 通信细节

- **协议:** HTTP/1.1 GET，局域网明文传输
- **接口地址:** `http://<pc-ip>:<port>/api/status`
- **默认端口:** 8266（`DEFAULT_HTTP_PORT`），每个设备槽位可独立配置不同端口
- **轮询间隔:** 3000ms（`POLL_INTERVAL_MS`）
- **请求超时:** 2000ms（`HTTP_TIMEOUT_MS`）

### 5.5 USB 串口通信细节

- **波特率:** 115200 bps
- **帧界定:** 每条 JSON 消息以换行符 `\n` 结尾
- **接收缓冲:** `JSON_BUF_SIZE = 1024` 字节（v1.3 由 512 扩容，支持农历 UTF-8 完整字段）
- **推送间隔:** 2 秒（可配置）

### 5.6 ESP32-S3 原生 USB CDC

通过 `USE_CDC_SERIAL` 宏控制，`DATA_SERIAL` 抽象层统一上层访问，无需关心底层串口类型。

---

## 6. ESP 端软件架构

### 6.1 开发环境

- **IDE:** Arduino IDE 2.x
- **ESP32 Core:** ESP32 Arduino Core 3.3.10
- **UI 框架:** LVGL v9.3（项目本地 `lv_conf.h` 覆盖系统库配置）
- **UI 设计工具:** SquareLine Studio 1.6.1（导出 LVGL C 代码）
- **显示驱动:** TFT_eSPI（底层 SPI flush 回调）
- **其他依赖:** ArduinoJson、TimeLib、TFT_Touch、DHT11

### 6.2 工程结构

```
PCDash/
├── PCDash.ino              # 主入口 setup() / loop()
├── config.h                # GPIO 定义、参数常量
├── data_model.h            # 统一数据结构体 (MonitorData / AppState)
├── lv_conf.h               # LVGL 功能配置（项目本地，覆盖系统库文件）
│
├── app_binddata.cpp        # 数据绑定层：MonitorData → LVGL 控件
├── app_binddata.h
├── data_hub.ino            # 双通道数据合并（Serial 优先）
├── serial_recv.ino         # 串口接收、帧解析、JSON → MonitorData
├── wifi_client.ino         # WiFi 连接/重连、HTTP 轮询、多 PC 管理、串口命令
├── ntp.ino                 # NTP 时间同步（每 30 分钟）
├── dht_sensor.ino          # DHT11 室内温湿度采样（GPIO37，10秒间隔）
├── cc_led.ino              # CC 状态 LED 控制（红/黄/绿 PWM）
├── settings.ino            # EEPROM 配置持久化
│
├── font_yahei14.cpp        # 桥接：雅黑 14pt（独立 TU 防静态变量名冲突）
├── font_shuziyahei48.cpp   # 桥接：数字雅黑 48pt（仅 0-9 和 :，11字符）
├── drivers_src.cpp         # 桥接：lv_port_disp + lv_port_indev
│
├── patch_ui.ps1            # SquareLine 导出后 C++兼容性补丁脚本（幂等）
│
├── ui/                     # SquareLine Studio 导出文件（勿手动修改）
│   ├── ui.h / ui.c
│   ├── ui_scrmain.h / ui_scrmain.c
│   ├── ui_helpers.c
│   ├── ui_comp.c / ui_comp.h
│   ├── ui_comp_hook.c
│   ├── ui_comp_pnl_statusbar.c
│   └── ui_font_*.c         # 自定义字体文件
│
└── drivers/
    ├── lv_port_disp.h / lv_port_disp.cpp   # LVGL flush → TFT_eSPI
    └── lv_port_indev.h / lv_port_indev.cpp # LVGL touch → TFT_Touch
```

> **编译说明:** Arduino IDE 只编译 sketch 根目录文件，子目录（`ui/`、`drivers/`）通过根目录的 `*_src.cpp` 桥接文件以 `#include` 方式拉入同一编译单元。字体文件因含大量同名 `static` 变量，各自用独立桥接文件放入独立翻译单元（TU）避免名称冲突。

### 6.3 SquareLine Studio 导出补丁

SquareLine Studio 生成 C 文件按 C++ 模式编译时存在三类兼容性问题，通过 `patch_ui.ps1` 脚本自动修复（幂等，每次重新导出后运行）：

| 问题类型 | 受影响文件 | 修复方式 |
|---------|----------|---------|
| 未启用 Widget API（dropdown/roller/slider/keyboard/textarea/spinbox） | ui_helpers.c | `#if LV_USE_xxx` 条件编译保护 |
| `void*` 隐式转换 | ui_helpers.c / ui_comp.c / ui_comp_*.c | 显式强制转换 `(Type*)` |
| 枚举类型不匹配（lv_obj_flag_t / lv_event_code_t） | ui_helpers.c / ui_comp.c | 显式强制转换 `(EnumType)` |

### 6.4 数据结构 (data_model.h)

```cpp
struct WthData {
  int  temp;       // °C
  int  humidity;   // %
  char icon[8];
  char desc[16];   // 天气描述（支持中文 UTF-8）
  char city[16];
  char wind[24];   // 风向+风力，如"东北风 3级"
};

struct MonitorData {
  char id[16];
  float cpu;
  int   cpuTemp;     // °C，-1 不可用
  int   cpuFreq;     // MHz，0 不可用
  int   cpuPower;    // W，0 不可用
  MemData mem;
  NetData net;
  GpuData gpu;       // gpu.name[24] 存储显卡型号
  bool  gpuValid;
  char  cpuName[32]; // CPU 型号，如 "i7-12700KF"
  char  timeStr[12]; // "HH:MM:SS"（NTP 不可用时回退）
  char  dateStr[12]; // "YYYY-MM-DD"
  char  bgHash[9];   // 背景图哈希（8字符+终止符）
  WthData wth;
  bool  wthValid;
  LunData lun;
  bool  lunValid;
  unsigned long lastUpdate;
  bool  connected;
  byte  source;      // 0=无, 1=WiFi, 2=Serial
};

// AppState（v1.5：移除 brightness，硬件无调光电路）
struct AppState {
  SystemState sysState;
  byte currentPage;
  byte currentPC;
  byte pcCount;
  bool autoScroll;
  bool darkTheme;
  // byte brightness;  // v1.5 移除，GPIO0 strapping pin 无调光电路
  unsigned long lastPageSwitch;
  unsigned long lastManualSwitch;
  bool forceRedraw;
  bool clockTick;
  int  roomTemp;       // DHT11 室温（°C，含 -2°C 修正）
  int  roomHumidity;   // DHT11 室内湿度（%）
  bool roomValid;      // DHT11 数据是否有效
};
```

### 6.5 数据绑定层 (app_binddata.cpp)

负责将 `MonitorData` 映射到 LVGL 控件，核心函数：

| 函数 | 负责区域 |
|------|---------|
| `bind_statusbar()` | 状态栏：网速、设备名、连接类型 |
| `bind_cpu()` | CPU 面板：型号标题、USG/TMP/PWR/MEM 进度条+数值+颜色 |
| `bind_gpu()` | GPU 面板：型号标题、USG/TMP/PWR/MEM 进度条+数值+颜色 |
| `bind_lower()` | 下半区：日期、天气四控件+风力、室内温湿度（DHT11）、农历 |
| `bind_dots()` | 底部页码指示点 |
| `app_binddata_tick_clock()` | 时钟刷新（NTP 优先，串口回退） |
| `set_bar_color()` | 进度条颜色辅助函数（绿/黄/红阈值判断） |

**时钟显示逻辑（NTP 优先 + 串口回退）：**

```
if (NTP 已同步) → 显示 NTP 本地时间 HH:MM
else if (d->timeStr 非空) → 取串口下发时间前 5 位 "HH:MM"
else → 显示 "--:--"
```

**天气及室内控件映射：**

| LVGL 控件 | 字段 | 字体 |
|----------|------|------|
| ui_lblcity | wth.city | 雅黑 14pt |
| ui_lblweather | wth.desc | 雅黑 14pt |
| ui_lblwthtemp | wth.temp（格式：28°C） | Montserrat 14pt |
| ui_lblwthhumidity | wth.humidity（格式：65%） | Montserrat 14pt |
| ui_lblwind | wth.wind | 雅黑 14pt |
| ui_lblroomtemp | app.roomTemp（格式：25°C） | Montserrat 14pt |
| ui_lblroomhumidity | app.roomHumidity（格式：48%） | Montserrat 14pt |

> 温湿度数值使用 Montserrat 字体，避免中文字体不含 U+B0（°）字符的 glyph warning。室内数据来自 DHT11 本地传感器，无效时显示 "--"。

### 6.6 主循环逻辑 (loop)

`loop()` 中各任务按 `millis()` 时间戳非阻塞执行：

1. `lv_timer_handler()` — LVGL 渲染定时
2. 串口接收 — 每次 loop 检查
3. 触摸检测 — 每次 loop（滑动手势切换设备）
4. WiFi 轮询 — 每 3 秒
5. NTP 同步 — 每 30 分钟
6. DHT11 采样 — 每 10 秒
7. 数据绑定更新 — 每 200ms（`app_binddata_update`）
8. 时钟刷新 — 每 1 秒（`app_binddata_tick_clock`）

### 6.7 LVGL 与 TFT_eSPI 协作关系

```
SquareLine Studio
    │ 导出 LVGL C 代码
    ▼
LVGL v9.3 (渲染引擎)
    │ flush_cb 回调（lv_port_disp.cpp）
    ▼
TFT_eSPI (SPI 驱动)
    │ SPI 总线
    ▼
ILI9341 TFT 屏幕
```

LVGL 在内部 SRAM 维护 1/10 屏大小（约 15KB）的帧缓冲区，每次脏区渲染完成后通过 `pushImageDMA()` 异步 flush 到屏幕。触摸事件由 `lv_port_indev.cpp` 经 `read_cb` 回调注入 LVGL 输入子系统。

### 6.8 lv_conf.h 本地覆盖机制

Arduino IDE 编译时头文件搜索优先级：**sketch 根目录 > 子目录 > 系统库目录**。项目根目录下放置 `lv_conf.h` 可覆盖系统库中的同名文件，实现项目级独立配置（启用字体、调整缓冲区大小等），不影响其他项目。

已启用字体：
- `LV_FONT_MONTSERRAT_12` — 状态栏、CPU/GPU 行标签与数值、日期、CC 状态
- `LV_FONT_MONTSERRAT_14` — CPU/GPU 标题、天气温湿度、室内温湿度数值

自定义字体（通过桥接 `.cpp` 编译）：
- `ui_font_yahei14`（雅黑 14pt）— 星期、农历、城市/天气描述/风向
- `ui_font_shuziyahei48`（数字雅黑 48pt，仅 `0-9:`）— 时钟大字

> `ui_font_shuziyahei48` 只含 11 个字符，flash 占用约 5~6 KB，远小于启用 `LV_FONT_MONTSERRAT_48`（约 40~70 KB）。

---

## 7. PC 端软件架构

### 7.1 工程结构

```
pcdash-server/
├── main.py                  # 入口点，数据采集主循环，托盘/无界面模式
├── collector/
│   ├── system_info.py       # CPU / 内存 / 网络 + 温度/主频/功率/型号
│   ├── gpu_info.py          # NVIDIA GPU (pynvml)
│   ├── weather_info.py      # 天气 + 自动定位 + 风向风力
│   └── lunar_info.py        # 农历完整信息 (sxtwl 离线计算)
├── transport/
│   ├── http_server.py       # Flask HTTP 服务 (/api/status)
│   └── serial_sender.py     # pyserial 数据推送 + 自动重连
├── config/
│   ├── config_manager.py
│   └── default_config.yaml
├── ui/
│   ├── tray_app.py
│   └── config_dialog.py
└── requirements.txt
```

### 7.2 依赖库

| 库 | 用途 | 平台 |
|----|------|------|
| psutil | CPU、内存、网络数据采集 | 全平台 |
| nvidia-ml-py | NVIDIA GPU 数据采集 | Windows / Linux |
| flask | 轻量级 HTTP Server | 全平台 |
| pyserial | USB 串口通信 | 全平台 |
| requests | 天气 API 和 IP 定位 | 全平台 |
| sxtwl | 农历完整计算（干支、节气，离线） | 全平台 |
| pystray | Windows 系统托盘图标 | Windows |
| Pillow | 托盘图标生成 | 全平台 |
| PyYAML | YAML 配置文件解析 | 全平台 |

### 7.3 CPU 型号采集 (system_info.py)

`SystemCollector.__init__()` 启动时调用 `_get_cpu_name()` 一次，结果缓存至 `self._cpu_name`，后续每次 `collect()` 直接附加到返回 dict，不重复执行：

| 平台 | 采集方式 |
|------|---------|
| Windows | `winreg` 读取 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString` |
| Linux | 读取 `/proc/cpuinfo` 中 `model name` 行 |
| 其他 | `platform.processor()` 回退 |

采集后去除 `(R)`/`(TM)` 并截断 `@ 3.60GHz` 后缀，通过正则提取核心型号（如 `i7-12700KF`），最终截断至 31 字节。

### 7.4 天气采集增强 (weather_info.py)

v1.3 新增风向风力字段，三渠道各自适配：

**辅助函数：**

```python
_deg_to_dir(deg)      # 角度 → 8方位中文（北/东北/东/...）
_ms_to_beaufort(ms)   # m/s → 蒲福风力等级（0-12级）
```

**各渠道 wind 字段处理：**

| 渠道 | 原始数据 | 处理 |
|------|---------|------|
| OpenWeatherMap | `wind.speed`(m/s) + `wind.deg`(°) | 两个辅助函数换算后拼接 |
| Open-Meteo | `wind_speed_10m`(km/h) + `wind_direction_10m`(°) | km/h ÷ 3.6 → m/s 后换算 |
| 和风天气 | `now.windDir`（中文）+ `now.windScale`（级数） | 直接拼接，最省 |

### 7.5 天气渠道策略

```
配置 provider = "qweather"  → 和风天气（需 api_key + api_host）
配置 provider = "open_meteo" → Open-Meteo（免费，无需 key）
默认 / openweathermap:
    有 api_key → OpenWeatherMap；失败 → 降级 Open-Meteo
    无 api_key → 直接 Open-Meteo
```

### 7.6 Windows CPU 温度/功率配置

参见 v1.2 §7.6（无变更）。通过 LibreHardwareMonitor Web Server（`http://localhost:8085/data.json`）获取 CPU Package 温度和功率，无需管理员权限。

### 7.7 Linux 服务模式

参见 v1.2 §7.7（无变更）。

---

## 8. 配置管理

### 8.1 PC 端配置文件 (YAML)

```yaml
device_name: ""

serial:
  enabled: true
  port: auto
  baud: 115200
  interval: 2

wifi:
  enabled: true
  host: 0.0.0.0
  port: 8266

gpu:
  enabled: true
  device_index: 0

weather:
  enabled: true
  provider: open_meteo       # openweathermap / open_meteo / qweather
  location: auto
  interval: 1800
  openweathermap:
    api_key: ""
  qweather:
    api_key: ""
    api_host: ""             # 如 devapi.qweather.com

background:
  enabled: false
  image_path: ""             # 本地图片路径，启动时加载到 /api/background
```

### 8.2 ESP 端配置 (EEPROM)

**EEPROM 存储布局（256 字节）：**

| 地址 | 长度 | 字段 |
|------|------|------|
| 0 | 1 | 有效标记 (0xAB) |
| 1 | 1 | （保留，原背光亮度，v1.5 废弃） |
| 2 | 1 | 主题 (0=暗, 1=亮) |
| 3 | 1 | 自动轮播 (0/1) |
| 10-41 | 32 | WiFi SSID |
| 42-73 | 32 | WiFi 密码 |
| 80-127 | 48 | PC IP 列表 (3×16 bytes) |
| 128 | 1 | PC 数量 |
| 129-134 | 6 | 每槽位 HTTP 端口 (3×2 bytes, uint16_t 小端) |

**串口配置命令：**

| 命令 | 格式 | 示例 | 说明 |
|------|------|------|------|
| WIFI | `WIFI:ssid,password` | `WIFI:MyNet,pass123` | 设置 WiFi 凭据 |
| PCIP | `PCIP:index,ip` | `PCIP:0,192.168.1.100` | 设置槽位 IP |
| PORT | `PORT:index,port` | `PORT:0,18266` | 设置槽位端口（v1.5 新增独立端口） |
| STATUS | `STATUS` | `STATUS` | 打印系统全状态（v1.5 新增） |

> **v1.5 变更：** `PORT` 命令从全局 `PORT:8266` 改为每槽位 `PORT:index,port`；新增 `STATUS` 命令。

---

## 9. 容错与异常处理

### 9.1 ESP 端状态机

| 状态 | 屏幕显示 | 进入条件 | 转出条件 |
|------|----------|----------|----------|
| BOOT | 启动初始化 | 上电 | 初始化完成 |
| WIFI_CONNECTING | "连接 WiFi..." | WiFi 配置存在 | 连接成功 / 超时 15秒 |
| WAITING_DATA | "等待数据..." | WiFi 已连接或串口已就绪 | 首次收到有效 JSON |
| RUNNING | 正常监控界面 | 收到有效数据 | 数据超时 > 30秒 |
| DATA_TIMEOUT | 界面提示断开 | 超过 30秒未收到数据 | 重新收到数据 |

### 9.2 PC 端容错

| 场景 | 处理方式 |
|------|---------|
| 无 NVIDIA GPU | `gpu` 字段置 null，服务正常运行 |
| LHM 未运行（Windows） | `cpu_t`/`cpu_w` 置 null，下次采集自动重试 |
| CPU 型号获取失败 | `cpu_name` 置空串，ESP 端回退显示 "CPU" |
| 天气 API 全部失败 | 保留上次缓存值，下次周期重试 |
| 风向风力字段缺失 | `wind` 置空串，ESP 端显示 "--" |
| 串口断开 | 重连线程每 5 秒重试 |
| WiFi 短暂断开 | 自动重连 + `wifiConnected` 标志恢复；断开超 30 秒周期性重试；重连成功后恢复轮询（v1.5 修复） |
| HTTP 连续失败 | 计数器累计 + 日志含 RSSI 信号强度，辅助排查网络问题 |
| DHT11 读取失败 | 关中断保护 + 失败自动重试 3 次；仍失败则 `app.roomValid = false`，UI 显示 "--"（v1.5 增强） |

---

## 10. OTA 与后续扩展

### 10.1 ArduinoOTA

WiFi 连接成功后自动启动 OTA 服务，后续固件更新无需拔线烧录。

### 10.2 后续版本可扩展方向

- **PSRAM 支持:** 待 Arduino ESP32 Core 修复 OPI PSRAM 初始化问题后启用，可使用双缓冲减少撕裂
- **黄历/宜忌:** 接入黄历 API（每天调用一次），宜/忌各取前 2-3 项显示在农历区域
- **Claude Code 状态感知:** CC Hooks → cc_hook.py → /api/cc-status → ESP 展示 AI 工作状态
- **背景图支持:** bgHash 字段已预留，ESP 端检测哈希变化后从 `/api/background` 下载并写入 SPIFFS
- **AMD GPU 支持:** 探索 AMD GPU 温度/使用率采集方案
- **MQTT 通信:** 替代 HTTP 轮询，实现更低延迟的双向通信
- **DHT22 升级:** DHT22 引脚兼容 DHT11，精度提升至 ±0.5°C / ±2%RH，仅需更换传感器和库

---

## 11. 开发里程碑

| 阶段 | 内容 | 状态 |
|------|------|------|
| M0: 最小原型 | ESP32 串口通信 + 基础显示 | 完成 |
| M1: 完整数据字段 | CPU温度/主频/农历/天气全部字段通信 | 完成 |
| M2: LVGL UI 重构 | 迁移 LVGL v9.3，SquareLine Studio 设计，双面板布局 | 完成 |
| M3: 数据绑定完善 | 天气四控件、风向风力、CPU/GPU型号、进度条动态颜色 | 完成 |
| M4: DHT11 + 字体优化 | 室内温湿度传感器、自定义数字字体、移除冗余按键定义 | 完成 |
| M5: WiFi + 多PC | WiFi 连接/重连修复、HTTP 轮询、触摸切换、每槽位独立端口、STATUS 命令 | 完成 |
| M6: 托盘与配置 | Windows 托盘应用、配置界面、Linux systemd | 进行中 |
| M7: 扩展功能 | PSRAM 支持、黄历宜忌、背景图、CC状态感知 | 待开始 |

---

## 附录

### A. 天气图标代码映射

**OWM 风格图标（Open-Meteo WMO 映射）：**

| 代码 | 含义 | WMO 天气码 |
|------|------|-----------|
| 01d | 晴天 | 0, 1 |
| 02d | 少云 | 2 |
| 04d | 阴天 | 3 |
| 09d | 毛毛雨 | 51-57 |
| 10d | 雨 | 61-67, 80-82 |
| 11d | 雷暴 | 95, 96, 99 |
| 13d | 雪 | 71-77, 85-86 |
| 50d | 雾霾 | 45, 48 |

### B. LVGL 进度条颜色 RGB 值

| 颜色 | RGB | 含义 |
|------|-----|------|
| 绿色 | (50, 200, 80) | 正常 |
| 黄色 | (220, 180, 0) | 警告 |
| 红色 | (220, 50, 50) | 危险/满载 |

### C. v1.4 → v1.5 变更记录

| 章节/模块 | 变更内容 |
|----------|---------|
| 封面 | 版本 v1.4 → v1.5，日期 2026-06-23 |
| 1.2 核心特性 | DHT11 描述增加关中断保护、重试、温度修正 |
| 3.1 显示屏 | 背光控制改为"GPIO0 始终常亮，无调光电路" |
| 3.2 GPIO | GPIO3 → GPIO0（TFT_BL_PIN），标注 strapping pin |
| 5.4 WiFi 通信 | 端口说明改为每槽位独立配置 |
| 6.4 数据结构 | AppState 移除 `brightness` 字段；`roomTemp` 标注含修正 |
| 8.2 ESP 端配置 | 新增完整 EEPROM 布局表；串口命令表新增 PORT 新格式 + STATUS 命令；端口从全局改为每槽位数组 `pc_ports[MAX_PC_COUNT]` |
| 9 容错 | 新增 WiFi 重连恢复机制说明；HTTP 连续失败日志增强；DHT11 增加关中断 + 重试 |
| 11 里程碑 | M5（WiFi + 多 PC）标记完成 |
| wifi_client.ino | **Bug 修复：** WiFi 断开后 `wifiConnected` 永远无法恢复为 true 导致轮询永久停止；新增 30 秒周期性重连；重连成功日志含断开时长；HTTP 失败日志含连续次数 + RSSI |
| data_hub.ino | 超时日志增强：打印 age 值、WiFi 状态；切换/全离线日志 |
| diag.ino | 5 分钟定期诊断增加 WiFi 状态、RSSI、每台 PC 连接状态及数据年龄 |
| DHT11 库 | `readRawData()` 关中断（`portENTER_CRITICAL`/`portEXIT_CRITICAL`）；`readByte()` 超时从 1000µs 放宽到 5000µs |
| dht_sensor.ino | 失败重试 3 次（间隔 100ms）；温度修正 -2°C；注释关闭正常读取日志 |
| 背光 | 移除 `lv_port_disp_set_brightness()` 函数及 `BL:` 串口命令；`config.h` 移除 `BL_DEFAULT`/`BL_STEP`；`data_model.h` 注释掉 `brightness` 字段；初始化改为 `digitalWrite(HIGH)` 常亮 |
| settings.ino | `http_port` 单变量 → `pc_ports[MAX_PC_COUNT]` 数组；EEPROM 布局 129-134 存 3 个 uint16_t；新增 `settings_savePort()` |
| bg_loader.ino | 背景图下载 URL 改用 `pc_ports[pcIndex]` |

### D. v1.3 → v1.4 变更记录（原附录 C）

| 章节/模块 | 变更内容 |
|----------|---------|
| 封面 | 版本 v1.3 → v1.4，日期 2026-06-16 |
| 1.2 核心特性 | 新增 DHT11 室内温湿度特性；按键改为触摸滑动 |
| 2.1 架构图 | 更新为 DHT11 传感器，移除 K1-K4 四按键 |
| 3.2 GPIO | 移除 K1/K2/K3（GPIO35/36/37）；GPIO37 改为 DHT11_PIN；补充 CC LED 引脚（38/39/40）；新增跳帽说明 |
| 4.1 监控数据项 | 新增室内温度、室内湿度两行（DHT11，10秒） |
| 4.2 UI 布局 | 天气行增加室内温湿度示意 |
| 4.4 交互方式 | 原"按键功能定义"改为"交互方式"（无物理按键，仅触摸滑动） |
| 6.1 开发环境 | 依赖新增 DHT11 库 |
| 6.2 工程结构 | 移除 buttons.ino/touch.ino；新增 dht_sensor.ino、cc_led.ino；字体桥接更新为 font_yahei14.cpp + font_shuziyahei48.cpp |
| 6.4 数据结构 | AppState 新增 roomTemp/roomHumidity/roomValid |
| 6.5 数据绑定层 | bind_lower() 描述补充室内温湿度；控件映射表新增 ui_lblroomtemp / ui_lblroomhumidity |
| 6.6 主循环逻辑 | 更新任务列表（移除按键，新增 DHT11 采样） |
| 6.8 lv_conf.h | 字体清单精确化：区分内置 Montserrat 和自定义字体；说明 shuziyahei48 体积优势 |
| 9.2 PC 端容错 | 新增 DHT11 读取失败降级行为 |
| 10.2 扩展方向 | 新增 DHT22 升级路径说明 |
| 11 开发里程碑 | 新增 M4（DHT11+字体优化，已完成），原 M4-M6 顺延为 M5-M7 |
| 启动日志 | setup() 新增 `[BUILD] __DATE__ __TIME__` 编译时间戳打印 |

---

### E. v1.2 → v1.3 变更记录

| 章节/模块 | 变更内容 |
|----------|---------|
| 1.2 核心特性 | 新增 LVGL v9.3 UI框架、动态颜色、天气拆分、风力风向、CPU/GPU型号、NTP串口回退特性描述 |
| 3.1 显示屏 | 精简为 ILI9341 单规格，移除 ST7735（当前实际硬件） |
| 3.2 GPIO | 更新为 ESP32-S3 实际引脚；新增 PSRAM 说明 |
| 4.1 监控数据项 | 新增 CPU型号、天气湿度、风向风力 行 |
| 4.2 UI 布局 | 新增单页双面板 ASCII 示意图 |
| 4.3 进度条颜色 | 新增详细阈值表（分使用率/温度/功率三类，GPU温度85°C告警） |
| 5.1 数据帧格式 | 新增 cpu_name / bgHash / wth.humidity / wth.wind 字段示例 |
| 5.2 字段说明 | 新增 v1.3 增量字段表 |
| 5.3 风向风力 | 新增三渠道拼接规则说明 |
| 5.5 串口细节 | JSON_BUF_SIZE 从 512 更新为 1024 |
| 6.1 开发环境 | 新增 LVGL、SquareLine Studio、TFT_Touch |
| 6.2 工程结构 | 全面更新为 LVGL 架构（桥接文件、补丁脚本、ui/目录）；移除旧 TFT_eSPI 绘制文件 |
| 6.3 SquareLine 补丁 | 新增章节，说明 C++ 兼容性问题类型及 patch_ui.ps1 |
| 6.4 数据结构 | 新增 WthData.humidity/wind/cpuName/bgHash 字段 |
| 6.5 数据绑定层 | 新章节，说明 app_binddata.cpp 各函数职责和天气控件映射 |
| 6.8 lv_conf.h | 新章节，说明项目本地覆盖机制和已启用字体 |
| 7.3 CPU型号采集 | 新增章节（三平台实现） |
| 7.4 天气采集增强 | 更新为含风向风力的版本，说明三渠道处理差异 |
| 9.2 PC端容错 | 新增 cpu_name / wind 失败降级行为 |
| 11 开发里程碑 | 重新梳理阶段，M2/M3 标注完成 |

---

*— 文档结束 —*
