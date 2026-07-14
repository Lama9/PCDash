# PCDash — 桌面PC实时监控屏幕 产品需求文档 (PRD)

> **版本:** v1.2  
> **日期:** 2026-06-10  
> **状态:** 迭代更新  
> **变更摘要:** 新增 CPU温度/主频/农历数据项；天气 API 增加 Open-Meteo 免费降级；补全 ESP/PC 工程结构；对齐 config.h 实际宏定义；更新依赖库清单。

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

PCDash 是一款桌面PC实时硬件监控屏幕。由一块基于 ESP 系列 MCU 驱动的 TFT 彩色显示屏作为显示终端，通过 WiFi 或 USB 串口与 PC 端 Python 服务通信，实时展示 CPU、内存、磁盘、网络、GPU 等核心硬件指标，同时显示实时时钟、农历信息和天气。

### 1.2 核心特性

- **双通道通信:** WiFi 和 USB 串口可同时工作，互不干扰
- **多平台 MCU:** 同一套代码兼容 ESP8266、ESP32、ESP32-S3，通过宏定义和编译选项切换
- **多屏幕支持:** 支持 ST7735 (160×128) 和 ILI9341 (320×240)，通过 `config.h` 编译宏切换
- **多 PC 监控:** WiFi 模式下可同时连接多台 PC，按键切换显示
- **轻量级 UI:** 基于 TFT_eSPI + TFT_eSprite 直接绘制，不依赖 LVGL 等重量级 GUI 框架，资源占用极低
- **自动+手动翻页:** 支持定时自动轮播和按键/触摸手动翻页
- **PC 端跨平台:** Python 实现，Windows 下任务栏托盘运行，Linux 下 systemd 服务运行
- **农历完整信息:** PC 端离线计算干支年月日、农历月日、节气，无需 API

### 1.3 目标用户

关注硬件状态的开发者、游戏玩家、服务器运维人员，希望在桌面上放置一块独立小屏幕，无需切换窗口即可实时监控 PC 运行状态。

---

## 2. 系统架构

### 2.1 架构总览

系统分为三层：数据采集层（PC/Python）、通信层（WiFi + USB）、显示层（ESP + TFT）。

```
┌─────────────────────┐     WiFi (HTTP GET)     ┌────────────────────────┐
│  PC-A Python Service │ ◄───────────────────────┤                        │
└─────────────────────┘                          │   ESP8266 / ESP32      │
┌─────────────────────┐     WiFi (HTTP GET)      │   / ESP32-S3           │
│  PC-B Python Service │ ◄───────────────────────┤                        │
└─────────────────────┘                          │   + ST7735 / ILI9341   │
┌─────────────────────┐     USB Serial (Push)    │   + K1-K4 四按键       │
│  PC-C Python Service │ ────────────────────────►│   + 触摸屏（可选）     │
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
| 部署便捷性 | 需配置 WiFi 凭据和 PC IP | 插上即用 |

两种通道可同时工作。当两个通道同时收到同一台 PC 的数据时，USB 串口数据优先（延迟更低、更可靠）。

---

## 3. 硬件设计

### 3.1 显示屏模块

| 参数 | ST7735 规格 | ILI9341 规格 |
|------|-------------|--------------|
| 屏幕尺寸 | 1.8 寸 TFT | 2.8 寸 TFT |
| 原始分辨率 | 128 × 160 (RGB565) | 240 × 320 (RGB565) |
| 横屏使用 | **160 × 128** (`setRotation(1/3)`) | **320 × 240** (`setRotation(3)`) |
| 驱动芯片 | ST7735 | ILI9341 |
| 通信接口 | SPI | SPI |
| 背光控制 | BLK 引脚，PWM 调光 | BLK 引脚，PWM 调光 |
| 触摸屏 | 无 | XPT2046（可选） |

config.h 中通过宏选择屏幕类型（两者只能选一）：

```cpp
// #define USE_ST7735      // 1.8寸 ST7735 160×128
#define USE_ILI9341        // 2.8寸 ILI9341 320×240
```

### 3.2 引脚定义 (J1 插座, 8-Pin)

| 引脚号 | 信号名 | 功能 |
|--------|--------|------|
| 1 | GND | 接地 |
| 2 | VCC | 电源 3.3V |
| 3 | SCL | SPI 时钟 (SCK) |
| 4 | SDA | SPI 数据 (MOSI) |
| 5 | RES | 复位 |
| 6 | DC | 数据/命令选择 |
| 7 | CS | 片选 |
| 8 | BLK | 背光控制 (PWM) |

### 3.3 按键 (K1-K4)

屏幕模块板载 K1-K4 四个按键插针柱，分别连接到 ESP 的 GPIO 引脚。具体 GPIO 编号因平台和接线而异，在 `config.h` 中定义。按键采用低电平有效，ESP 端开启内部上拉 (`INPUT_PULLUP`)。

### 3.4 MCU 平台对比

| 特性 | ESP8266 | ESP32 | ESP32-S3 |
|------|---------|-------|----------|
| CPU | 1核 80/160MHz | 2核 240MHz | 2核 240MHz |
| RAM | ~80KB 可用 | 520KB | 512KB |
| PSRAM | 不支持 | 可选 4/8MB | 可选 2/8MB |
| WiFi | 2.4GHz | 2.4GHz | 2.4GHz |
| 蓝牙 | 无 | BT 4.2 + BLE | BLE 5.0 |
| USB | 需外置 USB-UART 芯片 | 需外置 USB-UART 芯片 | 原生 USB-OTG (CDC) |
| SPI | HSPI | VSPI / HSPI | 多路 SPI |
| 建议场景 | 最低成本方案 | **主力推荐方案** | 需要原生 USB 时选用 |

**ESP32 为首选推荐平台。** 相比 ESP8266，RAM 翻数倍，双核可让网络和渲染分核运行；相比 ESP32-S3，价格更低且社区生态更成熟。ESP32-S3 的优势在于原生 USB-OTG，可省去外置 USB-UART 芯片。

### 3.5 GPIO 分配 (config.h)

各平台的 SPI 引脚和按键 GPIO 通过 `config.h` 中的平台宏定义统一管理：

| GPIO | ESP32-S3 | ESP32 | ESP8266 |
|------|----------|-------|---------|
| TFT 背光 (TFT_BL_PIN) | 3 | 2 | 16 |
| K1 (KEY1_PIN) | 35 | 34 | 4 |
| K2 (KEY2_PIN) | 36 | 35 | 5 |
| K3 (KEY3_PIN) | 37 | 32 | 不可用 |
| K4 (KEY4_PIN) | 38 | 33 | 不可用 |
| 触摸 CS (TOUCH_CS_PIN) | 1 | — | — |

4按键对2.8寸 ILI9341 320x240 触摸屏无效

---

## 4. 功能需求

### 4.1 监控数据项

| 数据项 | 数据源 | 刷新频率 | 说明 |
|--------|--------|----------|------|
| CPU 使用率 | `psutil.cpu_percent()` | 2-3秒 | 百分比，精度 0.1% |
| CPU 实时温度 | Windows: LHM Web Server API；Linux/macOS: `psutil.sensors_temperatures()` | 2-3秒 | °C 整数；不可用时为 `null` |
| CPU 实时主频 | `psutil.cpu_freq()` | 2-3秒 | MHz 整数；不可用时为 `null` |
| CPU 实时功率 | Windows: LHM Web Server API；Linux: Intel RAPL sysfs | 2-3秒 | W 整数；不支持平台返回 `null` |
| 内存使用率 | `psutil.virtual_memory()` | 2-3秒 | 百分比 + 已用/总量(GB) |
| 磁盘使用率 | `psutil.disk_usage()` | 2-3秒 | 百分比 + 已用/总量(GB) |
| 网络上传速度 | `psutil.net_io_counters()` 差值 | 2-3秒 | bytes/s，显示时换算 KB/MB |
| 网络下载速度 | `psutil.net_io_counters()` 差值 | 2-3秒 | bytes/s |
| GPU 使用率 | pynvml (NVML) | 2-3秒 | 无 NVIDIA GPU 时为 `null` |
| GPU 温度 | pynvml (NVML) | 2-3秒 | °C |
| 显存使用率 | pynvml (NVML) | 2-3秒 | 百分比 |
| 显卡型号 | pynvml (NVML) | 启动时读取 | 字符串，最长 24 字节 |
| GPU 实时功率 | pynvml `nvmlDeviceGetPowerUsage()` | 2-3秒 | W 整数；传感器不可用时为 `null` |
| 实时时间 | ESP NTP 自主维护（WiFi 模式）；PC `time` 字段作无 WiFi 时回退 | 每秒本地刷新 / 30分钟 NTP 同步 | HH:MM:SS |
| 实时日期 | ESP NTP 自主维护（WiFi 模式）；PC `date` 字段作无 WiFi 时回退 | 同上 | YYYY-MM-DD |
| 农历日期 | `sxtwl` 离线计算 | 每次推送携带 | 干支年月日 + 农历月日 + 节气 |
| 实时天气 | Open-Meteo / OpenWeatherMap（PC 端采集） | 30 分钟 | 温度 + 天气图标 + 描述 + 城市 |

### 4.2 按键功能定义

| 按键 | 短按功能 | 长按功能 (>1.5s) |
|------|----------|------------------|
| K1 (上) | 上一页 | 自动轮播开/关 |
| K2 (下) | 下一页 | 切换亮/暗主题 |
| K3 (确认) | 切换 PC（多 PC 模式） | 进入设置页 |
| K4 (取消) | 背光亮度调节（循环） | 关闭背光 |

按键支持硬件消抖（50ms）和软件消抖双重保障。短按/长按判定阈值在 `config.h` 中定义（`DEBOUNCE_MS = 50`，`LONG_PRESS_MS = 1500`）。按键功能对2.8寸 ILI9341 320x240 触摸屏无效。

### 4.3 多页面设计

| 页面 | 内容 | 核心元素 |
|------|------|----------|
| Page 0: 总览页 | CPU + MEM + GPU 概览 + 时间 | 进度条 + 数字 + 时钟 |
| Page 1: CPU 详情 | CPU 使用率 + 温度 + 主频 | 大字数字 + 进度条 + 颜色标识 |
| Page 2: 存储和网络 | 磁盘使用 + 上传/下载速度 | 数字 + 进度条 + 速度值 |
| Page 3: GPU 详情 | GPU 使用率 + 温度 + 显存 + 型号 | 大字数字 + 温度计 |
| Page 4: 天气 + 农历 | 天气图标 + 温度 + 描述 + 城市 + 农历 | 图标 + 文字 |

> **注:** Page 4 农历显示区域（干支年月日 + 节气）的具体布局待 ESP 端 UI 重新设计时确定，PC 端数据已完整下发。

**状态栏：** 所有页面顶部统一显示状态栏：当前 PC 名称、连接状态图标（WiFi/USB）、当前页码指示器。

**翻页模式：**

- **手动翻页:** K1/K2 短按切换上/下一页
- **自动轮播:** 默认每 5 秒切换下一页，间隔可配置，K1 长按开关

两种模式可共存。自动轮播开启时，手动翻页后暂停自动轮播一个周期，然后恢复。

---

## 5. 通信协议设计

### 5.1 数据帧格式 (JSON)

WiFi 和 USB 串口使用统一的 JSON 数据格式。字段名采用缩写以减少传输大小和 ESP 端解析内存开销：

```json
{
  "id":    "My-PC",
  "cpu":   45.2,
  "cpu_t": 65,
  "cpu_f": 3600,
  "cpu_w": 95,
  "mem":   { "pct": 51.2, "used": 16.4, "total": 32.0 },
  "dsk":   { "pct": 50.0, "used": 125,  "total": 446  },
  "net":   { "up": 14832, "dn": 5007 },
  "gpu":   { "usage": 78, "temp": 65, "mem_pct": 42, "name": "RTX 3080", "watt": 220 },
  "time":  "14:30:25",
  "date":  "2026-06-10",
  "lun":   { "gz_y": "丙午年", "gz_m": "壬午月", "gz_d": "甲子日",
             "mo": "四月", "dy": "廿五", "jq": "芒种" },
  "wth":   { "temp": 18, "icon": "02d", "desc": "Partly Cloudy", "city": "Adachi" }
}
```

### 5.2 字段说明

| 字段 | 类型 | 单位 | 说明 |
|------|------|------|------|
| id | string | — | PC 标识名，用于多 PC 区分；取自配置 `device_name`，留空时自动使用系统主机名 |
| cpu | float | % | CPU 总体使用率，精度 0.1% |
| cpu_t | int / null | °C | CPU 实时温度；不可用时为 `null` |
| cpu_f | int / null | MHz | CPU 实时主频（当前频率）；不可用时为 `null` |
| cpu_w | int / null | W | CPU Package 实时功率；Windows=LHM Web API、Linux=Intel RAPL；不支持时为 `null` |
| mem.pct | float | % | 内存使用百分比 |
| mem.used / mem.total | float | GB | 已用/总内存，精度 0.1GB |
| dsk.pct | float | % | 磁盘使用百分比 |
| dsk.used / dsk.total | int | GB | 已用/总磁盘 |
| net.up / net.dn | int | bytes/s | 网络上传/下载速度 |
| gpu.usage | int | % | GPU 计算单元使用率 |
| gpu.temp | int | °C | GPU 温度 |
| gpu.mem_pct | int | % | 显存使用率 |
| gpu.name | string | — | 显卡型号，最长 24 字节 |
| gpu.watt | int / null | W | GPU 实时功率；传感器不可用时为 `null` |
| time | string | — | 时分秒，格式 "HH:MM:SS"；ESP WiFi 模式下以 NTP 本地时间为主，此字段仅在无 WiFi（串口模式）时作回退显示 |
| date | string | — | 年月日，格式 "YYYY-MM-DD"；同 `time`，串口模式下的回退日期来源 |
| lun.gz_y | string | — | 干支年，如 "丙午年"（以立春为岁首） |
| lun.gz_m | string | — | 干支月，如 "壬午月" |
| lun.gz_d | string | — | 干支日，如 "甲子日" |
| lun.mo | string | — | 农历月，如 "四月" / "闰四月" |
| lun.dy | string | — | 农历日，如 "廿五" |
| lun.jq | string | — | 节气名，如 "芒种"；非节气日为空字符串 `""` |
| wth.temp | int | °C | 实时气温 |
| wth.icon | string | — | 天气图标代码（OWM 风格，如 "02d"） |
| wth.desc | string | — | 天气描述（英文，如 "Partly Cloudy"） |
| wth.city | string | — | 城市名 |

**空值规则：** 某项数据不可用时对应字段置为 `null`（如无 NVIDIA 显卡则 `gpu: null`，LHM 未运行则 `cpu_t: null`）。ESP 端对 `null` 字段显示 "N/A"。

### 5.3 WiFi 通信细节

- **协议:** HTTP/1.1 GET，局域网明文传输
- **接口地址:** `http://<pc-ip>:<port>/api/status`
- **默认端口:** 8266（`DEFAULT_HTTP_PORT`）
- **轮询间隔:** 3000ms（`POLL_INTERVAL_MS`）
- **请求超时:** 2000ms（`HTTP_TIMEOUT_MS`，防止阻塞 loop）
- **多 PC 轮询:** 依次请求各 PC 接口，单台超时则标记离线跳过
- **响应格式:** Content-Type: application/json

### 5.4 USB 串口通信细节

- **波特率:** 115200 bps（`SERIAL_BAUD`）
- **数据格式:** 8N1（标准）
- **帧界定:** 每条 JSON 消息以换行符 `\n` 结尾，ESP 端按字节读取至 `\n` 后触发解析
- **接收缓冲:** `JSON_BUF_SIZE = 512` 字节（`config.h`）
- **JSON 解析文档:** `StaticJsonDocument<896>`（`serial_recv.ino`）
- **推送间隔:** 2 秒（可配置），PC 端主动推送
- **COM 口识别:** 支持自动扫描（按 VID/PID 识别 CH340/CP2104）或手动配置

### 5.5 ESP32-S3 原生 USB CDC 特殊处理

ESP32-S3 支持原生 USB CDC，无需外置 USB-UART 芯片。代码中通过 `USE_CDC_SERIAL` 宏控制：

```cpp
// config.h 中定义
#define USE_CDC_SERIAL 0   // 0 = 使用 UART(Serial)，1 = 使用 CDC(USBSerial)

#if USE_CDC_SERIAL && HAS_NATIVE_USB
  #define DATA_SERIAL  USBSerial
#else
  #define DATA_SERIAL  Serial
#endif
```

上层业务逻辑统一使用 `DATA_SERIAL`，无需关心底层串口类型。

---

## 6. ESP 端软件架构

### 6.1 开发环境

- **IDE:** Arduino IDE 2.x
- **核心库:** TFT_eSPI, ArduinoJson, WiFi/ESP8266WiFi, HTTPClient/ESP8266HTTPClient
- **开发语言:** C++ (Arduino)

选择 Arduino 而非 ESP-IDF 的理由：TFT_eSPI、ArduinoJson 等核心依赖都是 Arduino 生态库；现有天气时钟项目已验证 Arduino 环境下的多平台兼容方案；项目复杂度不需要 RTOS 级别的任务调度。

### 6.2 工程结构

```
PCDash/
├── PCDash.ino              # 主入口 setup() / loop()
├── config.h                # 平台检测、屏幕选择、GPIO 定义、参数常量
├── data_model.h            # 统一数据结构体 (MonitorData / AppState)
├── tft_setup_st7735.h      # ST7735 TFT_eSPI 引脚配置（含于 User_Setup.h）
├── tft_setup_ili9341.h     # ILI9341 TFT_eSPI 引脚配置
├── display.ino             # TFT 初始化、背光 PWM 控制、Sprite 管理
├── ui_pages.ino            # 各页面绘制实现（总览、CPU、存储、GPU、天气）
├── ui_draw.ino             # 绘制原语（进度条、图标、主题色）
├── wifi_client.ino         # WiFi 连接、HTTP 轮询、多 PC 管理、串口命令处理
├── serial_recv.ino         # 串口接收、帧解析、JSON → MonitorData
├── data_hub.ino            # 数据汇聚（合并 WiFi + Serial 数据源，Serial 优先）
├── buttons.ino             # 按键扫描、消抖、短按/长按判定
├── touch.ino               # 触摸屏（XPT2046）滑动手势检测（ILI9341 专用）
├── ntp.ino                 # NTP 时间同步（WiFi 模式下启用）
├── settings.ino            # 配置持久化 (EEPROM)
├── font/
│   └── FxLED_32.h          # LED 风格数字字体
└── img/                    # 图标位图头文件（待添加）
```

> **说明:** Arduino IDE 会将同一目录下的所有 `.ino` 文件按字母序合并后编译。全局变量和函数声明在 `.h` 文件中，各 `.ino` 文件可互相调用。

### 6.3 数据结构 (data_model.h)

```cpp
struct MonitorData {
  char id[16];
  float cpu;          // CPU 使用率 (%)
  int   cpuTemp;      // CPU 温度 (°C)，-1 表示不可用
  int   cpuFreq;      // CPU 主频 (MHz)，0 表示不可用
  MemData mem;        // 内存 { pct, used, total }
  DskData dsk;        // 磁盘 { pct, used, total }
  NetData net;        // 网络 { up, dn } (bytes/s)
  GpuData gpu;        // GPU { usage, temp, mem_pct, name[24] }
  bool gpuValid;      // GPU 数据是否有效
  char timeStr[12];   // "HH:MM:SS"
  char dateStr[12];   // "YYYY-MM-DD"
  // lun 字段：ESP 端 UI 重新设计后添加，当前接收到 JSON 中但暂不解析
  WthData wth;        // 天气 { temp, icon[8], desc[16], city[16] }
  bool wthValid;
  unsigned long lastUpdate;
  bool connected;
  byte source;        // 0=无, 1=WiFi, 2=Serial
};
```

### 6.4 平台兼容层设计 (config.h)

通过 `config.h` 中的宏定义统一处理平台差异：

```cpp
// ======== 屏幕选择 ========
// #define USE_ST7735      // 1.8寸 ST7735 160×128
#define USE_ILI9341        // 2.8寸 ILI9341 320×240

#ifdef USE_ST7735
  #define SCREEN_W  160
  #define SCREEN_H  128
  #define SCREEN_ROTATION  1
#elif defined(USE_ILI9341)
  #define SCREEN_W  320
  #define SCREEN_H  240
  #define SCREEN_ROTATION  3
#endif

// ======== 串口选择 ========
#define USE_CDC_SERIAL  0  // 1 = ESP32-S3 原生 USB CDC

#if USE_CDC_SERIAL && HAS_NATIVE_USB
  #define DATA_SERIAL  USBSerial
#else
  #define DATA_SERIAL  Serial
#endif

// ======== 通用常量 ========
#define SERIAL_BAUD       115200
#define JSON_BUF_SIZE     512     // 串口接收字符缓冲（字节）
#define POLL_INTERVAL_MS  3000    // WiFi HTTP 轮询间隔
#define HTTP_TIMEOUT_MS   2000    // HTTP 请求超时
#define DATA_TIMEOUT_MS   30000   // 数据超时判定
#define WIFI_TIMEOUT_MS   15000   // WiFi 连接超时
#define DEFAULT_HTTP_PORT 8266
#define PAGE_COUNT        5       // 总页数
#define MAX_PC_COUNT      3       // 最多监控 PC 数量
#define AUTO_SCROLL_MS    5000    // 自动轮播间隔
#define DEBOUNCE_MS       50      // 按键消抖
#define LONG_PRESS_MS     1500    // 长按判定
#define BL_DEFAULT        60      // 默认亮度 0-100
#define BL_STEP           20      // 每次调节步进
```

### 6.5 平台差异处理要点

| 差异点 | 处理方式 |
|--------|----------|
| SPI 引脚编号 | 各平台在 config.h 中定义不同的 GPIO 编号 |
| 串口类型 | `DATA_SERIAL` 宏抽象，`USE_CDC_SERIAL=1` 启用 ESP32-S3 CDC |
| WiFi 库 | ESP8266 用 `ESP8266WiFi.h`，ESP32 系列用 `WiFi.h` |
| HTTP 客户端 | ESP8266 用 `ESP8266HTTPClient`，ESP32 用 `HTTPClient` |
| LittleFS | ESP8266 需要 `LittleFS.h`，ESP32 已内置 |
| 双核任务 | ESP32/S3 可将网络放 Core 0、渲染放 Core 1；ESP8266 单核顺序执行 |
| ArduinoOTA | 三个平台均支持，统一开启 |
| JSON 解析 | `StaticJsonDocument<896>`（serial_recv.ino），可放 PSRAM 版本待扩展 |
| 触摸屏 | ILI9341 扩展板支持 XPT2046，ST7735 无触摸 |

### 6.6 主循环逻辑 (loop)

`loop()` 中的任务按非阻塞方式定时执行，不使用 `delay()`，采用 `millis()` 时间戳判断：

1. **按键扫描:** 每 20ms 检查一次按键状态，硬件消抖 + 软件消抖
2. **触摸检测:** 每次 loop 检查触摸事件（ILI9341 平台）
3. **串口接收:** 每次 loop 检查 `DATA_SERIAL.available()`，有数据则读取解析
4. **WiFi 轮询:** 每 3 秒触发一次 HTTP GET（多 PC 按顺序依次请求）
5. **NTP 同步:** 每 30 分钟同步一次系统时间
6. **数据合并:** DataHub 将 WiFi 和 Serial 数据合并到统一数据模型（Serial 优先）
7. **屏幕刷新:** 数据更新后触发当前页面的局部重绘
8. **自动轮播:** 如开启，每 5 秒自动切换到下一页

### 6.7 屏幕绘制策略

沿用现有天气时钟项目的 **TFT_eSprite** 局部刷新模式，避免全屏刷新闪烁：

**局部重绘 (Sprite) 标准模式：**

```cpp
clk.createSprite(width, height);
clk.fillSprite(bgColor);
clk.setTextDatum(ML_DATUM);
clk.setTextColor(fontColor, bgColor);
clk.drawString(value, x, y);
clk.pushSprite(screenX, screenY);
clk.deleteSprite();
```

**进度条颜色阈值：**

| 使用率 | 颜色 | RGB565 |
|--------|------|--------|
| 0–60% | 绿色（正常） | 0x07E0 |
| 60–85% | 黄色（警告） | 0xFFE0 |
| 85–100% | 红色（高负载） | 0xF800 |

GPU 温度阈值：< 60°C 绿色 / 60–80°C 黄色 / > 80°C 红色。

### 6.8 字体方案

TFT_eSPI 内置点阵字体用于英文和数字显示。所有标签用英文缩写（CPU、MEM、DSK、NET、GPU），避免中文字库占用 Flash。复用现有天气时钟项目中的 `FxLED_32.h` LED 风格数字字体用于监控数值大字显示。

> **农历中文显示:** ESP 端 UI 重新设计时，可参考 demo 的 `nonglifront.h` 精简农历专用字体（仅含天干地支和农历常用汉字，体积远小于通用中文字库）。

---

## 7. PC 端软件架构

### 7.1 工程结构

```
pcdash-server/
├── main.py                  # 入口点，数据采集主循环，托盘/无界面模式分发
├── collector/
│   ├── __init__.py
│   ├── system_info.py       # CPU / 内存 / 磁盘 / 网络 + CPU温度/主频/功率 (psutil + LHM Web API)
│   ├── gpu_info.py          # NVIDIA GPU (pynvml)
│   ├── weather_info.py      # 天气 + 自动定位 (Open-Meteo / OpenWeatherMap)
│   └── lunar_info.py        # 农历完整信息，干支年月日 + 节气 (sxtwl 离线计算)
├── transport/
│   ├── __init__.py
│   ├── http_server.py       # Flask HTTP 服务 (/api/status)
│   └── serial_sender.py     # pyserial 数据推送 + 自动重连
├── config/
│   ├── config_manager.py    # 配置加载/保存/验证
│   └── default_config.yaml  # 默认配置模板
├── ui/                      # Windows 专用
│   ├── tray_app.py          # pystray 系统托盘图标
│   └── config_dialog.py     # tkinter 配置界面
├── service/                 # Linux 专用
│   └── pcdash.service       # systemd 服务文件
└── requirements.txt
```

### 7.2 依赖库

| 库 | 用途 | 平台 |
|----|------|------|
| psutil | CPU、内存、磁盘、网络数据采集 | 全平台 |
| nvidia-ml-py | NVIDIA GPU 数据采集 (NVML 官方绑定) | Windows / Linux |
| flask | 轻量级 HTTP Server | 全平台 |
| pyserial | USB 串口通信 | 全平台 |
| requests | 天气 API 和 IP 定位 API 调用 | 全平台 |
| sxtwl | 农历完整计算（干支、节气，离线） | 全平台 |
| pystray | Windows 系统托盘图标 | Windows (Linux/macOS 可选) |
| Pillow | 托盘图标生成 | 全平台 |
| PyYAML | YAML 配置文件解析 | 全平台 |

### 7.3 数据采集流程

1. **启动时:** 加载配置文件 → 初始化 psutil / pynvml → IP 自动定位 → 首次获取天气
2. **每 2 秒循环:**
   - 采集 CPU 使用率、CPU 温度（平台适配）、CPU 主频
   - 采集内存、磁盘、网络速度
   - 采集 GPU（pynvml，失败时为 `null`）
   - 计算农历信息（sxtwl 离线，极低开销）
3. **网络速度计算:** 前后两次采样 `net_io_counters()` 做差值除以时间间隔
4. **天气采集:** 每 30 分钟请求一次天气 API，缓存结果；请求失败时保留上次缓存
5. **数据分发:** HTTP Server 保存最新 JSON 供 ESP 轮询；Serial Sender 主动写入串口

### 7.4 天气与自动定位

**自动定位方案：** PC 端启动时通过 IP 地理定位 API 自动获取当前城市，无需手动配置。优先级：

1. **配置文件手动覆盖：** `config.yaml` 中指定 `weather.lat`/`weather.lon` 或 `weather.city`
2. **自动定位：** 调用 `ip-api.com/json/`（免费，无需 Key）→ 返回 city, lat, lon
3. **定位缓存：** 结果缓存到内存，重启服务时重新定位

**天气 API 策略（双 API 降级）：**

```
优先：OpenWeatherMap（config.yaml 中配置 api_key 后生效）
降级：Open-Meteo（完全免费，无需注册，自动使用 ip-api 定位的经纬度）
```

| 场景 | 使用的 API |
|------|-----------|
| `api_key` 非空且请求成功 | OpenWeatherMap |
| `api_key` 非空但请求失败 | 自动降级到 Open-Meteo |
| `api_key` 为空 | 直接使用 Open-Meteo |

Open-Meteo 接口：`https://api.open-meteo.com/v1/forecast?latitude=&longitude=&current=temperature_2m,weather_code&timezone=auto`，返回 WMO 天气码，映射为 OWM 风格图标字符串。

### 7.5 Windows 托盘应用

Windows 平台下，Python 服务以后台进程运行，通过 pystray 在任务栏右下角显示托盘图标。右键菜单功能：

- **打开配置:** 弹出 tkinter 配置窗口，可修改端口、串口、天气城市、API Key 等
- **查看状态:** 显示当前 HTTP 服务状态、串口连接状态
- **退出:** 关闭服务并退出程序

启动方式推荐用 `pythonw.exe` 运行以避免命令行窗口。也可用 PyInstaller 打包为单个 `.exe` 方便分发。支持 `--headless` 参数强制无界面模式。

### 7.6 Windows CPU 温度/功率配置

Windows 内核不对用户态程序开放 CPU 温度传感器，`psutil.sensors_temperatures()` 在 Windows 下返回空字典。需借助 **LibreHardwareMonitor (LHM)** 作为硬件访问代理，通过其内置 **Web Server** 读取传感器数据。**无需管理员权限，无需额外 Python 包。**

#### 配置流程

```
第一步：下载 LibreHardwareMonitor
  https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases
  下载最新版，解压到任意目录（如 C:\Tools\LHM\）

第二步：启动 LHM 并启用 Web Server
  运行 LibreHardwareMonitor.exe
  菜单 → Options → Start web server  ✓（默认端口 8085）

第三步：配置开机自启（推荐）
  LHM 菜单 → Options → Run On Windows Startup  ✓
  LHM 菜单 → Options → Start Minimized          ✓
```

#### 工作原理

LHM 在 `http://localhost:8085/data.json` 提供完整传感器树（JSON 格式）。Python 通过 `requests.get()` 拉取并递归解析，提取 `Temperatures/CPU Package` 和 `Powers/CPU Package` 节点的值。无 COM/WMI 依赖，无需管理员权限。

#### 降级行为

| 情形 | `cpu_t` / `cpu_w` 返回值 |
|------|--------------------------|
| LHM 运行中且 Web Server 已启用 | CPU Package 温度（°C）/ 功率（W） |
| LHM 未运行或 Web Server 未启用 | `null`（HTTP 连接失败静默处理） |
| Linux / macOS | 温度由 `psutil.sensors_temperatures()` 提供；功率 Linux=RAPL，macOS=`null` |

### 7.7 Linux 服务模式

Linux 平台下以 systemd 服务运行，配置通过 YAML 文件管理：

```ini
[Unit]
Description=PCDash Monitor Service
After=network.target

[Service]
Type=simple
User=<username>
ExecStart=/usr/bin/python3 /opt/pcdash/main.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

---

## 8. 配置管理

### 8.1 PC 端配置文件 (YAML)

默认配置文件 `default_config.yaml`，实际配置文件 `pcdash_config.yaml`（用户可覆盖）：

```yaml
device_name: ""               # 留空则自动使用系统主机名 (socket.gethostname())

serial:
  enabled: true
  port: auto              # auto = 自动扫描 CH340/CP2104；或手动填 "COM3"
  baud: 115200
  interval: 2             # 推送间隔 (秒)

wifi:
  enabled: true
  host: 0.0.0.0
  port: 8266

gpu:
  enabled: true
  device_index: 0         # 第几块 GPU（从 0 开始）

weather:
  enabled: true
  api_provider: openweathermap
  api_key: ""             # 留空则自动使用免费 Open-Meteo；填入后优先用 OWM
  location: auto          # auto = IP 自动定位
  # city: "Tokyo"         # 手动覆盖城市（可选）
  # lat: 35.6762          # 手动覆盖经纬度（可选，优先级高于城市名）
  # lon: 139.6503
  units: metric           # metric（摄氏度）/ imperial（华氏度）
  interval: 1800          # 天气刷新间隔 (秒)

disk:
  path: "C:\\"            # Windows 默认 C 盘；Linux 填 "/"
```

### 8.2 ESP 端配置 (EEPROM)

ESP 端配置存储在 EEPROM，布局如下：

```
地址 0:      背光亮度 (0-100)
地址 1-4:    保留
地址 10-29:  WiFi SSID (20 bytes)
地址 30-49:  WiFi Password (20 bytes)
地址 50-59:  PC-1 IP (10 bytes)
地址 60-69:  PC-2 IP
地址 70-79:  PC-3 IP
地址 80:     PC 数量
地址 81:     HTTP 端口低字节
地址 82:     HTTP 端口高字节
地址 83:     自动轮播开关 (0/1)
地址 84:     主题 (0=暗, 1=亮)
```

串口命令格式（PC 端发送给 ESP）：

| 命令格式 | 说明 |
|---------|------|
| `WIFI:ssid,password` | 配置 WiFi 凭据 |
| `PCIP:0,192.168.1.100` | 设置 PC-0 的 IP 地址 |
| `PORT:8266` | 设置 HTTP 轮询端口 |

---

## 9. 容错与异常处理

### 9.1 ESP 端状态机

| 状态 | 屏幕显示 | 进入条件 | 转出条件 |
|------|----------|----------|----------|
| BOOT | 启动 Logo + 进度条 | 上电 | WiFi 连接成功 / 超时 |
| WIFI_CONNECTING | "连接 WiFi..." + SSID | WiFi 配置存在 | 连接成功 / 超时 15秒 |
| WAITING_DATA | "等待数据..." | WiFi 已连接或串口已就绪 | 首次收到有效 JSON |
| RUNNING | 正常监控界面 | 收到有效数据 | 数据超时 > 30秒 |
| DATA_TIMEOUT | 界面变灰 + "断开连接" | 超过 30秒未收到数据 | 重新收到数据 |

当 WiFi 断开时自动重连，不影响 USB 串口通道的正常工作。串口通道断开时（USB 拔出），自动回退到仅 WiFi 模式。

### 9.2 PC 端容错

| 场景 | 处理方式 |
|------|---------|
| 无 NVIDIA GPU 或驱动缺失 | pynvml 初始化失败，`gpu` 字段置 `null`，服务正常运行 |
| CPU 温度/功率（Windows/LHM Web Server 未启用） | HTTP 连接异常被捕获，`cpu_t`/`cpu_w` 置 `null`，下次采集自动重试 |
| CPU 主频不可用 | `psutil.cpu_freq()` 返回 None，`cpu_f` 置 `null` |
| CPU 功率（Linux/非 Intel 或无 RAPL 权限） | `open()` 异常被捕获，`cpu_w` 置 `null` |
| GPU 功率传感器不可用 | `nvmlDeviceGetPowerUsage` 异常被捕获，`gpu.watt` 置 `null` |
| 天气 OWM 请求失败 | 自动降级到 Open-Meteo；都失败时保留上次缓存，下次周期重试 |
| IP 自动定位失败 | 回退到配置文件中的手动城市/经纬度；都无则天气功能禁用 |
| 串口断开 | 重连线程每 5 秒重试，日志记录失败原因 |
| 配置文件不存在/损坏 | 采用 default_config.yaml 默认值启动，托盘提示用户 |

---

## 10. OTA 与后续扩展

### 10.1 ArduinoOTA

从第一个版本起即开启 ArduinoOTA 支持。WiFi 连接成功后自动启动 OTA 服务，后续固件更新无需拔线烧录。建议设置 OTA 密码防止局域网内误操作。

### 10.2 后续版本可扩展方向

- **Claude Code 状态感知（开发中）:** 通过 CC Hooks 捕获 AI 工作状态，Python 服务端聚合后由 ESP 屏幕展示；详见 `docs/PRD-CC状态感知显示功能.md`
- **农历 ESP 端显示:** UI 重新设计时引入 `nonglifront.h` 精简字体，在 Page 4 展示干支年月日和节气
- **Web 配置页:** ESP 端开启 WebServer，用手机浏览器配置 WiFi、PC 列表、显示参数
- **CPU 温度（非 NVIDIA）:** 引入 AMD GPU 温度采集；Windows 下探索 OpenHardwareMonitor 方案
- **报警通知:** CPU/GPU 温度超阈值时屏幕闪烁或蜂鸣器提示
- **MQTT 通信:** 替代 HTTP 轮询，实现更低延迟的双向通信
- **Go 重写 PC 端:** 如有分发需求，用 Go 实现零依赖单文件分发
- **更大屏幕:** 支持 ILI9341 (320×240) 的多页面 UI（已有 `USE_ILI9341` 编译宏）

---

## 11. 开发里程碑

| 阶段 | 内容 | 交付物 | 状态 |
|------|------|--------|------|
| M0: 最小原型 | ESP32 横屏显示静态文字；PC 采集 CPU/MEM 通过串口发送；ESP 解析并显示 | 串口通信打通，横屏显示 CPU+MEM | 进行中 |
| M1: 完整数据 | CPU温度/主频、磁盘、网络、GPU、时间、农历、天气全部字段 | 全部监控数据可通过串口/HTTP 传输 | PC端已完成 |
| M2: 多页面 UI | 多页面架构；K1-K4 按键 + 触摸翻页；进度条/颜色/图标；自动+手动翻页 | 完整的多页面 UI + 按键交互 | 进行中 |
| M3: WiFi 通道 | WiFi 连接、HTTP 轮询；PC 端 Flask 服务；双通道并行工作 | WiFi + USB 同时工作 | 进行中 |
| M4: 多PC + 托盘 | 多 PC 轮询与按键切换；Windows 托盘应用；配置界面 | 完整的多 PC 监控 + Windows 托盘 | 待开始 |
| M5: 多平台兼容 | ESP8266 / ESP32-S3 适配；Linux systemd 服务；ArduinoOTA | 三平台编译通过 + Linux 服务 | 待开始 |

---

## 附录

### A. 天气图标代码映射

**OWM 风格图标（OpenWeatherMap / Open-Meteo WMO 映射）：**

| 代码 | 含义 | WMO 天气码 |
|------|------|-----------|
| 01d / 01n | 晴天 | 0, 1 |
| 02d | 少云 | 2 |
| 04d | 阴天 | 3 |
| 09d | 毛毛雨 | 51-57 |
| 10d | 雨 | 61-67, 80-82 |
| 11d | 雷暴 | 95, 96, 99 |
| 13d | 雪 | 71-77, 85-86 |
| 50d | 雾霾 | 45, 48 |

### B. RGB565 常用颜色值

| 颜色 | RGB565 值 | 用途 |
|------|-----------|------|
| 黑色 | 0x0000 | 背景 |
| 白色 | 0xFFFF | 文字 |
| 绿色 | 0x07E0 | 正常状态 |
| 黄色 | 0xFFE0 | 警告状态 |
| 红色 | 0xF800 | 高负载/高温 |
| 蓝色 | 0x001F | 强调 |
| 青色 | 0x07FF | 辅助 |
| 橙色 | 0xFD20 | 中等警告 |
| 深灰 | 0x4208 | 进度条背景 |
| 浅灰 | 0xC618 | 次要文字 |
| 深蓝 | 0x000B | 状态栏背景 |

### C. v1.1 → v1.2 变更记录

| 章节 | 变更内容 |
|------|---------|
| 1.2 核心特性 | 新增多屏幕支持、触摸屏、农历信息特性描述 |
| 3.1 显示屏模块 | 扩展为 ST7735 / ILI9341 双屏对比表；补充触摸屏信息 |
| 3.5 GPIO 分配 | 改为表格形式，按平台列出实际 GPIO 编号 |
| 4.1 监控数据项 | CPU温度补充 Windows/Linux 双路径说明；新增 CPU主频、农历、实时日期行 |
| 4.3 多页面设计 | Page 1 增加温度/主频；Page 4 增加农历；添加农历布局待定说明 |
| 5.1 数据帧格式 | 补全 `cpu_t`/`cpu_f`/`lun` 字段；使用真实示例数据 |
| 5.2 字段说明 | 补全所有新增字段；明确 `null` 语义 |
| 5.4 USB 串口 | 区分说明 JSON_BUF_SIZE（接收缓冲 512B）和 StaticJsonDocument（896B） |
| 5.5 CDC 处理 | 更新宏名称为 `DATA_SERIAL` / `USE_CDC_SERIAL` |
| 6.2 工程结构 | 补充 `touch.ino`/`ntp.ino`/`tft_setup_*.h` 实际文件 |
| 6.3 数据结构 | 新增 MonitorData 结构体字段列表（含 cpuTemp/cpuFreq）；注明 lun 待扩展 |
| 6.4 平台兼容层 | 更新 config.h 示例：宏名 DATA_SERIAL、屏幕选择、实际常量完整列表 |
| 7.1 工程结构 | 新增 `collector/lunar_info.py` |
| 7.2 依赖库 | 新增 `sxtwl` |
| 7.3 采集流程 | 补充 CPU温度/主频/农历采集步骤 |
| 7.4 天气与定位 | 更新为双 API 降级策略（OWM → Open-Meteo） |
| 7.6 CPU 温度配置 | 新增章节（原 7.6 Linux 顺延为 7.7） |
| 8.1 PC 配置文件 | 修正 `api_key` 默认为空、`disk.path` 默认为 `C:\\`；补充串口命令表 |
| 9.2 PC 端容错 | 改为表格形式；新增 CPU温度各降级场景说明 |
| 11 里程碑 | 新增状态列，反映当前进度 |

---

*— 文档结束 —*
