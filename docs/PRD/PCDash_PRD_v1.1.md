# PCDash — 桌面PC实时监控屏幕 产品需求文档 (PRD)

> **版本:** v1.1  
> **日期:** 2026-06-09  
> **状态:** 初稿

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

PCDash 是一款桌面PC实时硬件监控屏幕。由一块基于 ESP 系列 MCU 驱动的 1.8 寸 TFT 彩色显示屏作为显示终端，通过 WiFi 或 USB 串口与 PC 端 Python 服务通信，实时展示 CPU、内存、磁盘、网络、GPU 等核心硬件指标，同时显示实时时钟和天气信息。

### 1.2 核心特性

- **双通道通信:** WiFi 和 USB 串口可同时工作，互不干扰
- **多平台 MCU:** 同一套代码兼容 ESP8266、ESP32、ESP32-S3，通过宏定义和编译选项切换
- **多 PC 监控:** WiFi 模式下可同时连接多台 PC，按键切换显示
- **轻量级 UI:** 基于 TFT_eSPI + TFT_eSprite 直接绘制，不依赖 LVGL 等重量级 GUI 框架，资源占用极低
- **横屏显示:** 屏幕旋转 90°，有效分辨率 160×128（宽×高）
- **自动+手动翻页:** 支持定时自动轮播和按键手动翻页
- **PC 端跨平台:** Python 实现，Windows 下任务栏托盘运行，Linux 下 systemd 服务运行

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
└─────────────────────┘                          │   + ST7735 TFT         │
┌─────────────────────┐     USB Serial (Push)    │     160×128 横屏       │
│  PC-C Python Service │ ────────────────────────►│   + K1-K4 四按键       │
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

| 参数 | 规格 |
|------|------|
| 屏幕尺寸 | 1.8 寸 TFT 彩色显示屏 |
| 原始分辨率 | 128 × 160 像素 (RGB565) |
| 实际使用 | **横屏 160 × 128**（`tft.setRotation(1)` 或 `tft.setRotation(3)`） |
| 驱动芯片 | ST7735 |
| 通信接口 | SPI (SCL/SDA/CS/DC/RES) |
| 背光控制 | BLK 引脚，S8050 三极管驱动，支持 PWM 调光 |
| 按键 | K1-K4 四个独立按键（板载插针柱，连接至 ESP GPIO） |

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

### 3.5 GPIO 分配策略

各平台的 SPI 引脚和按键 GPIO 在代码中通过平台宏定义进行映射，具体定义在 `config.h` 中统一管理。参考现有天气时钟项目的引脚定义方式：

```cpp
// 参考现有项目的引脚定义风格
#define LCD_BL_PIN  0     // 背光 PWM
#define KEY1_PIN    34    // K1 按键
#define KEY2_PIN    35    // K2 按键
#define KEY3_PIN    32    // K3 按键 (待分配)
#define KEY4_PIN    33    // K4 按键 (待分配)
```

用户可根据实际接线修改 GPIO 编号而无需改动业务逻辑。这个按键只针对1.8寸 ST7735 160x128 4按键屏有效。

---

## 4. 功能需求

### 4.1 监控数据项

| 数据项 | 数据源 | 刷新频率 | 显示形式 |
|--------|--------|----------|----------|
| CPU 使用率 | psutil.cpu_percent() | 2-3秒 | 百分比数字 + 进度条 |
| CPU 实时温度 | psutil.sensors_temperatures() | 2-3秒 | 温度数字 + 颜色标识 |
| CPU 实时主频 | psutil.cpu_freq() | 2-3秒 | 主频数字 + 单位 |
| 内存使用率 | psutil.virtual_memory() | 2-3秒 | 百分比 + 已用/总量 |
| 磁盘使用率 | psutil.disk_usage() | 2-3秒 | 百分比 + 已用/总量 |
| 网络上传速度 | psutil.net_io_counters() 差值 | 2-3秒 | KB/s 或 MB/s |
| 网络下载速度 | psutil.net_io_counters() 差值 | 2-3秒 | KB/s 或 MB/s |
| GPU 使用率 | pynvml (NVML) | 2-3秒 | 百分比 + 进度条 |
| GPU 温度 | pynvml (NVML) | 2-3秒 | 温度数字 + 颜色标识 |
| 显存使用率 | pynvml (NVML) | 2-3秒 | 百分比 + 已用/总量 |
| 显卡型号 | pynvml (NVML) | 2-3秒 | 显卡型号 |
| 实时时间 | PC 系统时钟 | 每次推送携带 | 时分秒 |
| 农历日期 | sxtwl 离线计算 | 每次推送携带 | 干支年月日 + 农历月日 + 节气 |
| 实时天气 | 天气 API（PC 端采集） | 30 分钟 | 天气图标 + 温度 |

### 4.2 按键功能定义

| 按键 | 短按功能 | 长按功能 (>1.5s) |
|------|----------|------------------|
| K1 (上) | 上一页 | 自动轮播开/关 |
| K2 (下) | 下一页 | 切换亮/暗主题 |
| K3 (确认) | 切换 PC（多 PC 模式） | 进入设置页 |
| K4 (取消) | 背光亮度调节（循环） | 关闭背光 |

按键支持硬件消抖（推荐 50ms）和软件消抖双重保障。短按/长按的判定阈值可配置。参考现有项目的按键检测模式（`digitalRead` + 状态变量）。

### 4.3 多页面设计

横屏 160×128 的屏幕无法同时显示所有监控项，因此采用多页面架构：

| 页面 | 内容 | 核心元素 |
|------|------|----------|
| Page 0: 总览页 | CPU + MEM + GPU 概览 + 时间 | 进度条 + 数字 + 时钟 |
| Page 1: CPU 详情 | CPU 使用率 + 各核心数据 (可选) | 大字数字 + 进度条 |
| Page 2: 存储和网络 | 磁盘使用 + 上传/下载速度 | 数字 + 进度条 + 速度值 |
| Page 3: GPU 详情 | GPU 使用率 + 温度 + 显存 + 型号 | 大字数字 + 温度计 |
| Page 4: 天气页 | 天气图标 + 温度 + 描述 + 城市 | 图标 + 文字 |

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
  "id": "my-pc",
  "cpu": 45.2,
  "cpu_t": 65,
  "cpu_f": 3600,
  "mem": { "pct": 51.2, "used": 16.4, "total": 32.0 },
  "dsk": { "pct": 50.0, "used": 512, "total": 1024 },
  "net": { "up": 1234, "dn": 56789 },
  "gpu": { "usage": 78, "temp": 65, "mem_pct": 42, "name": "RTX 3080" },
  "time": "14:30:25",
  "date": "2026-06-09",
  "lun": { "gz_y": "丙午年", "gz_m": "壬午月", "gz_d": "甲子日", "mo": "四月", "dy": "廿五", "jq": "芒种" },
  "wth": { "temp": 28, "icon": "01d", "desc": "Clear", "city": "Tokyo" }
}
```

### 5.2 字段说明

| 字段 | 类型 | 单位 | 说明 |
|------|------|------|------|
| id | string | — | PC 标识名，用于多 PC 区分 |
| cpu | float | % | CPU 总体使用率 |
| cpu_t | int / null | °C | CPU 实时温度；Windows 通常为 null |
| cpu_f | int / null | MHz | CPU 实时主频（当前频率） |
| mem.pct | float | % | 内存使用百分比 |
| mem.used / mem.total | float | GB | 已用/总内存 |
| dsk.pct | float | % | 磁盘使用百分比 |
| dsk.used / dsk.total | int | GB | 已用/总磁盘 |
| net.up / net.dn | int | bytes/s | 网络上传/下载速度 |
| gpu.usage | int | % | GPU 计算单元使用率 |
| gpu.temp | int | °C | GPU 温度 |
| gpu.mem_pct | int | % | 显存使用率 |
| gpu.name | string | — | 显卡型号 |
| time / date | string | — | 时分秒 / 年月日 |
| lun.gz_y | string | — | 干支年，如 "丙午年"（以立春为岁首） |
| lun.gz_m | string | — | 干支月，如 "壬午月" |
| lun.gz_d | string | — | 干支日，如 "甲子日" |
| lun.mo   | string | — | 农历月，如 "四月" / "闰四月" |
| lun.dy   | string | — | 农历日，如 "廿五" |
| lun.jq   | string | — | 节气名，如 "芒种"；非节气日为空字符串 "" |
| wth.temp | int | °C | 实时温度 |
| wth.icon | string | — | 天气图标代码 |
| wth.desc | string | — | 天气描述 (Clear, Rain 等) |
| wth.city | string | — | 城市名 |

当某项数据不可用时（如无 NVIDIA 显卡），对应字段置为 `null`，ESP 端显示 "N/A"。

### 5.3 WiFi 通信细节

- **协议:** HTTP/1.1 GET，局域网明文传输（ESP 做 TLS 开销太大）
- **接口地址:** `http://<pc-ip>:<port>/api/status`
- **默认端口:** 8266
- **轮询间隔:** 2-3 秒，可配置
- **请求超时:** 2 秒（防止阻塞 loop）
- **多 PC 轮询:** 依次请求各 PC 接口，单台超时则标记离线跳过
- **响应格式:** Content-Type: application/json

### 5.4 USB 串口通信细节

- **波特率:** 115200 bps
- **数据格式:** 8N1（标准）
- **帧界定:** 每条 JSON 消息以换行符 `\n` 结尾，ESP 端用 `readStringUntil('\n')` 读取一整行
- **推送间隔:** 2-3 秒，PC 端主动推送
- **COM 口识别:** 支持自动扫描（按 VID/PID 识别 CH340/CP2104）或手动配置

### 5.5 ESP32-S3 原生 USB CDC 特殊处理

ESP32-S3 支持原生 USB CDC，无需外置 USB-UART 芯片。代码中通过宏定义区分：ESP32-S3 使用 `USBSerial` (CDC) 接口，其他平台使用硬件 UART (`Serial`)。两者 API 兼容，上层业务逻辑无需修改。

---

## 6. ESP 端软件架构

### 6.1 开发环境

- **IDE:** Arduino IDE 2.x
- **核心库:** TFT_eSPI, ArduinoJson, WiFi/ESP8266WiFi, HTTPClient/ESP8266HTTPClient
- **开发语言:** C++ (Arduino)

选择 Arduino 而非 ESP-IDF 的理由：TFT_eSPI、ArduinoJson 等核心依赖都是 Arduino 生态库；现有天气时钟项目已验证 Arduino 环境下的多平台兼容方案；项目复杂度不需要 RTOS 级别的任务调度。

### 6.2 工程结构

采用 Arduino IDE 标准工程结构——多 `.ino` 文件同目录自动合并编译，与现有天气时钟项目风格一致：

```
PCDash/
├── PCDash.ino              # 主入口 setup() / loop()
├── config.h                # 平台检测、GPIO 定义、参数常量
├── data_model.h            # 统一数据结构体 (MonitorData)
├── display.ino             # TFT 初始化、背光控制、Sprite 管理
├── ui_pages.ino            # 各页面绘制实现（总览、CPU、存储、GPU、天气）
├── ui_draw.ino             # 绘制原语（进度条、图标绘制、主题色）
├── wifi_client.ino         # WiFi 连接、HTTP 轮询、多 PC 管理
├── serial_recv.ino         # 串口接收、帧解析
├── data_hub.ino            # 数据汇聚（合并 WiFi + Serial 数据源）
├── buttons.ino             # 按键扫描、消抖、短按/长按判定
├── settings.ino            # 配置持久化 (EEPROM / LittleFS)
├── font/                   # 字体头文件
│   ├── FxLED_32.h          # LED 风格数字字体 (可复用现有项目)
│   └── ...
└── img/                    # 图标位图头文件
    ├── wifi_icon.h
    ├── usb_icon.h
    ├── weather_icons.h     # 天气图标集
    └── ...
```

> **说明:** Arduino IDE 会将同一目录下的所有 `.ino` 文件按字母序合并后编译。全局变量和函数声明在 `.h` 文件中，各 `.ino` 文件可互相调用。这种结构与现有天气时钟项目的 `#pragma region` 分块方式等价，但文件拆分更清晰。

### 6.3 平台兼容层设计

通过 `config.h` 中的宏定义统一处理平台差异，沿用现有天气时钟项目已验证的模式：

```cpp
// config.h — 平台检测与差异化定义

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #define PLATFORM_NAME    "ESP8266"
  #define HAS_DUAL_CORE    0
  #define HAS_PSRAM        0
  #define HAS_NATIVE_USB   0
  #define SERIAL_PORT      Serial
  #define PIN_SPI_SCK      14    // HSPI
  #define PIN_SPI_MOSI     13
  // ...

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #define PLATFORM_NAME    "ESP32-S3"
  #define HAS_DUAL_CORE    1
  #define HAS_PSRAM        1
  #define HAS_NATIVE_USB   1
  #define SERIAL_PORT      USBSerial   // USB CDC
  // ...

#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #define PLATFORM_NAME    "ESP32"
  #define HAS_DUAL_CORE    1
  #define HAS_PSRAM        1
  #define HAS_NATIVE_USB   0
  #define SERIAL_PORT      Serial      // HW UART
  // ...

#else
  #error "未识别的平台，请确认编译环境"
#endif

// 通用常量
#define SCREEN_W         160   // 横屏宽
#define SCREEN_H         128   // 横屏高
#define SCREEN_ROTATION  1     // 或 3，根据实际屏幕朝向
#define JSON_BUF_SIZE    512   // ArduinoJson buffer
#define POLL_INTERVAL_MS 3000  // WiFi 轮询间隔
#define SERIAL_BAUD      115200
```

### 6.4 平台差异处理要点

| 差异点 | 处理方式 |
|--------|----------|
| SPI 引脚编号 | 各平台在 config.h 中定义不同的 GPIO 编号 |
| 串口类型 | ESP32-S3 用 `USBSerial` (CDC)，其他用 `Serial` (UART) |
| WiFi 库 | ESP8266 用 `ESP8266WiFi.h`，ESP32 系列用 `WiFi.h` |
| HTTP 客户端 | ESP8266 用 `ESP8266HTTPClient`，ESP32 用 `HTTPClient` |
| LittleFS | ESP8266 需要 `LittleFS.h`，ESP32 已内置 |
| 双核任务 | ESP32/S3 可将网络放 Core 0、渲染放 Core 1；ESP8266 单核顺序执行 |
| ArduinoOTA | 三个平台均支持，统一开启 |
| 内存策略 | ESP8266 严格控制 JSON buffer 大小；ESP32 可使用 PSRAM |

### 6.5 主循环逻辑 (loop)

`loop()` 中的任务按非阻塞方式定时执行，不使用 `delay()`，沿用 `millis()` 时间戳判断的模式（与现有天气时钟项目一致）：

1. **按键扫描:** 每 20ms 检查一次按键状态，硬件消抖 + 软件消抖
2. **串口接收:** 每次 loop 检查 `SERIAL_PORT.available()`，有数据则读取解析
3. **WiFi 轮询:** 每 2-3 秒触发一次 HTTP GET（多 PC 按顺序依次请求）
4. **数据合并:** DataHub 将 WiFi 和 Serial 数据合并到统一数据模型（Serial 优先）
5. **屏幕刷新:** 数据更新后触发当前页面的局部重绘
6. **自动轮播:** 如开启，每 N 秒自动切换到下一页

### 6.6 屏幕绘制策略

沿用现有天气时钟项目的 **TFT_eSprite** 局部刷新模式，避免全屏刷新闪烁：

**初始化绘制:** 切换到某个页面时，绘制一次完整的静态框架（背景、分区线、固定标签文字 "CPU" "MEM" 等、图标）。使用 `tft.fillScreen()` 清屏后重绘。

**局部重绘 (Sprite):** 每次收到新数据后，对变化的区域创建 Sprite，在 Sprite 内绘制新内容，然后 `pushSprite` 到指定坐标。Sprite 绘制完后立即 `deleteSprite` 释放内存。这种方式完全无闪烁。参考现有代码模式：

```cpp
// 与现有天气时钟项目相同的 Sprite 绘制模式
clk.createSprite(width, height);
clk.fillSprite(bgColor);
clk.setTextDatum(ML_DATUM);
clk.setTextColor(fontColor, bgColor);
clk.drawString(value, x, y);
clk.pushSprite(screenX, screenY);
clk.deleteSprite();
```

**进度条实现:** 两个 `fillRect` 叠加：深色背景矩形（满宽）+ 彩色前景矩形（宽度 = 满宽 × 百分比 / 100）。颜色根据阈值动态切换：

- 0-60%: 绿色 (0x07E0) — 正常
- 60-85%: 黄色 (0xFFE0) — 警告
- 85-100%: 红色 (0xF800) — 高负载

GPU 温度的颜色阈值另定：60°C 以下绿色 / 60-80°C 黄色 / 80°C 以上红色。

**页面切换:** 切换页面时先 `fillScreen` 全屏清空，然后调用新页面的初始化绘制 + 数据填充。仅在页面切换时才有一次全屏重绘，正常数据刷新时不会闪烁。

### 6.7 字体方案

TFT_eSPI 内置点阵字体用于英文和数字显示。所有标签用英文缩写（CPU、MEM、DSK、NET、GPU），避免中文字库占用 Flash。可复用现有天气时钟项目中的 `FxLED_32.h` 等 LED 风格数字字体。大字数字可用 TFT_eSPI 的 `loadFont` / smooth font 功能实现更好的视觉效果。

### 6.8 图标方案

小图标（CPU 芯片、温度计、WiFi 信号、USB 标识、天气等）做成位图数组 (bitmap array)，通过 `drawBitmap` 或 `pushImage` 绘制。建议图标尺寸 16x16 或 12x12 像素，每个图标仅占用几十字节 Flash。天气图标可参考现有项目的 JPEG 解码方案（TJpg_Decoder），也可简化为位图图标以节省内存。

---

## 7. PC 端软件架构

### 7.1 工程结构

```
pcdash-server/
├── main.py                  # 入口点
├── collector/
│   ├── __init__.py
│   ├── system_info.py       # CPU / 内存 / 磁盘 / 网络 (psutil)
│   ├── gpu_info.py          # NVIDIA GPU (pynvml)
│   └── weather_info.py      # 天气 + 自动定位
├── transport/
│   ├── __init__.py
│   ├── http_server.py       # Flask HTTP 服务 (/api/status)
│   └── serial_sender.py     # pyserial 数据推送
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
| psutil | CPU、内存、磁盘、网络数据采集 | Windows / Linux / macOS |
| pynvml | NVIDIA GPU 数据采集 (NVML 官方绑定) | Windows / Linux |
| flask | 轻量级 HTTP Server | 全平台 |
| pyserial | USB 串口通信 | 全平台 |
| requests | 天气 API 和 IP 定位 API 调用 | 全平台 |
| pystray | Windows 系统托盘图标 | Windows (Linux/macOS 可选) |
| Pillow | 托盘图标生成 | 全平台 |
| PyYAML | YAML 配置文件解析 | 全平台 |
| wmi | Windows CPU 温度读取（需配合 LibreHardwareMonitor） | Windows (可选) |

### 7.3 数据采集流程

1. **启动时:** 加载配置文件 → 初始化 psutil / pynvml → 自动定位获取城市 → 首次获取天气
2. **每 2 秒循环:** 采集 CPU、内存、磁盘、网络、GPU 数据 → 组装 JSON
3. **网络速度计算:** 前后两次采样 `net_io_counters()` 做差值除以时间间隔
4. **天气采集:** 每 30 分钟请求一次天气 API，缓存结果
5. **数据分发:** HTTP Server 保存最新 JSON 供 ESP 轮询；Serial Sender 主动写入串口

### 7.4 天气与自动定位

**自动定位方案:** PC 端启动时通过 IP 地理定位 API 自动获取当前城市，无需手动配置。方案优先级：

1. **ip-api.com** (免费，无需 API Key): `http://ip-api.com/json/` → 返回 city, lat, lon
2. **配置文件手动覆盖:** 如果 `config.yaml` 中指定了 `weather.city` 或 `weather.lat`/`weather.lon`，则优先使用手动配置
3. **缓存:** 定位结果缓存到内存，不每次天气刷新都重新定位。重启服务时重新定位。

**天气 API:** 使用 OpenWeatherMap 免费档，以自动定位获取的经纬度或城市名查询。

> **参考:** 现有天气时钟项目使用 `cn.apihz.cn` 的 IP 定位 + `wgeo.weather.com.cn` 的城市代码方案。PCDash 的天气采集在 PC 端完成（不在 ESP 端），因此可直接使用更标准的国际 API，也可保留中国天气网方案作为中国用户的备选。

### 7.5 Windows 托盘应用

Windows 平台下，Python 服务以后台进程运行，通过 pystray 在任务栏右下角显示托盘图标。右键菜单提供以下功能：

- **打开配置:** 弹出 tkinter 配置窗口，可修改端口、串口、天气城市、API Key 等
- **查看状态:** 显示当前 HTTP 服务状态、串口连接状态、已连接的 ESP 设备数
- **开机自启:** 可选注册自启动项
- **退出:** 关闭服务并退出程序

启动方式推荐用 `pythonw.exe` 运行以避免命令行窗口。也可用 PyInstaller 打包为单个 `.exe` 方便分发。

### 7.6 Windows CPU 温度配置

Windows 内核不对用户态程序开放 CPU 温度传感器，`psutil.sensors_temperatures()` 在 Windows 下返回空字典。需借助 **LibreHardwareMonitor (LHM)** 作为硬件访问代理，通过其暴露的 WMI 命名空间读取温度数据。

#### 安装流程

```
第一步：下载 LibreHardwareMonitor
  https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases
  下载最新 LibreHardwareMonitor.zip，解压到任意目录（如 C:\Tools\LHM\）

第二步：以管理员权限启动 LHM
  右键 LibreHardwareMonitor.exe → 以管理员身份运行

第三步：配置开机自启（推荐）
  LHM 菜单 → Options → Run On Windows Startup  ✓
  LHM 菜单 → Options → Start Minimized          ✓

第四步：安装 Python WMI 绑定
  pip install wmi
```

#### 工作原理

LHM 运行后向系统注册 WMI 命名空间 `root\LibreHardwareMonitor`，Python 通过 `wmi.WMI()` 查询其中 `Sensor` 类，筛选 `SensorType == "Temperature"` 且名称含 `"CPU Package"` 的传感器即可获取 CPU Die 封装温度。

#### 降级行为

| 情形 | `cpu_t` 返回值 |
|------|--------------|
| LHM 运行中 + `wmi` 已安装 | CPU Package 温度（°C 整数） |
| LHM 未运行 | `null` |
| `wmi` 未安装 | `null` |
| Linux / macOS | 由 `psutil.sensors_temperatures()` 提供 |

LHM 意外退出后，下一次采集会自动重试 WMI 连接，无需重启 PCDash 服务。

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

```yaml
# pcdash_config.yaml
device_name: "My-PC"

serial:
  enabled: true
  port: auto              # auto = 自动扫描 CH340/CP2104
  baud: 115200
  interval: 2             # 推送间隔 (秒)

wifi:
  enabled: true
  host: 0.0.0.0
  port: 8266

gpu:
  enabled: true
  device_index: 0         # 第几块 GPU

weather:
  enabled: true
  api_provider: openweathermap   # 可选: openweathermap / cnweather
  api_key: "your_api_key"        # OpenWeatherMap API Key
  location: auto                 # auto = IP 自动定位; 或手动填城市名/经纬度
  # city: "Tokyo"                # 手动覆盖城市
  # lat: 35.6762                 # 手动覆盖经纬度
  # lon: 139.6503
  units: metric                  # metric / imperial
  interval: 1800                 # 采集间隔 (秒)

disk:
  path: "/"              # Linux: / , Windows: C:\
```

### 8.2 ESP 端配置 (EEPROM / LittleFS)

ESP 端配置存储方案沿用现有天气时钟项目的 EEPROM 方式（简单可靠），后续版本可迁移到 LittleFS JSON：

```
EEPROM 存储布局:
  地址 0:    背光亮度 (0-100)
  地址 1-4:  保留
  地址 10-29: WiFi SSID (20 bytes)
  地址 30-49: WiFi Password (20 bytes)  
  地址 50-59: PC-1 IP (10 bytes, 如 "192168110")
  地址 60-69: PC-2 IP
  地址 70-79: PC-3 IP
  地址 80:    PC 数量
  地址 81:    轮询间隔 (秒)
  地址 82:    自动轮播开关 (0/1)
  地址 83:    轮播间隔 (秒)
  地址 84:    主题 (0=暗, 1=亮)
```

初次启动时若 EEPROM 数据无效，使用默认配置。可通过串口命令修改（参考现有项目的串口设置模式），或后续版本通过 Web 配置页面修改。

---

## 9. 容错与异常处理

### 9.1 ESP 端状态机

| 状态 | 屏幕显示 | 进入条件 | 转出条件 |
|------|----------|----------|----------|
| BOOT | 启动 Logo + 进度条 | 上电 | WiFi 连接成功 / 超时 |
| WIFI_CONNECTING | "连接 WiFi..." + SSID | WiFi 配置存在 | 连接成功 / 超时 15秒 |
| WAITING_DATA | "等待数据..." | WiFi 已连接或串口已就绪 | 首次收到有效 JSON |
| RUNNING | 正常监控界面 | 收到有效数据 | 数据超时 >30秒 |
| DATA_TIMEOUT | 界面变灰 + "断开连接" | 超过 30秒未收到数据 | 重新收到数据 |

启动进度条可参考现有天气时钟项目的 `loading()` 函数实现。

当 WiFi 断开时自动重连，不影响 USB 串口通道的正常工作。串口通道断开时（USB 拔出），自动回退到仅 WiFi 模式。

### 9.2 PC 端容错

- **GPU 不可用:** 如果 pynvml 初始化失败（无 NVIDIA 显卡或未装驱动），JSON 中 `gpu` 字段置 `null`，ESP 端显示 "N/A"
- **天气 API 失败:** 网络请求失败时保留上次缓存结果，下次周期重试
- **自动定位失败:** IP 定位 API 不可用时，回退到配置文件中的手动城市设置；都没有则天气功能禁用
- **串口断开:** 每 5 秒重试连接，日志记录失败原因
- **配置文件损坏:** 采用默认配置启动，托盘提示用户

---

## 10. OTA 与后续扩展

### 10.1 ArduinoOTA

从第一个版本起即开启 ArduinoOTA 支持。WiFi 连接成功后自动启动 OTA 服务，后续固件更新无需拔线烧录。建议设置 OTA 密码防止局域网内误操作。

### 10.2 后续版本可扩展方向

- **Web 配置页:** ESP 端开启 WebServer，用手机浏览器访问配置 WiFi、添加 PC 列表、调整显示参数
- **更大屏幕:** 支持 ILI9341 (240x320) 等更高分辨率屏幕，通过 TFT_eSPI 的 User_Setup 切换屏幕驱动
- **自定义页面:** 允许用户自定义每页显示哪些数据项和布局
- **报警通知:** 当 CPU/GPU 温度超阈值时，屏幕闪烁或发出提示音（蜂鸣器）
- **Go 重写 PC 端:** 如有分发需求，可用 Go 重写 PC 服务，实现零依赖单文件分发
- **MQTT 通信:** 后续可引入 MQTT 代替 HTTP 轮询，实现更低延迟的双向通信

---

## 11. 开发里程碑

| 阶段 | 内容 | 交付物 | 估时 |
|------|------|--------|------|
| M0: 最小原型 | ESP32 + ST7735 横屏显示静态文字；PC Python 采集 CPU/MEM 通过串口发送；ESP 解析并显示 | 串口通信打通，横屏显示 CPU+MEM | 2-3 天 |
| M1: 完整数据 | 添加磁盘、网络、GPU、时间、天气采集（含自动定位）；完成全部 JSON 字段 | 全部监控数据可通过串口显示 | 3-4 天 |
| M2: 多页面 UI | 实现多页面架构；K1-K4 按键翻页/功能；进度条、颜色阈值、图标；自动+手动翻页 | 完整的多页面 UI + 按键交互 | 3-4 天 |
| M3: WiFi 通道 | 添加 WiFi 连接、HTTP 轮询；PC 端 Flask 服务；双通道并行工作 | WiFi + USB 同时工作 | 2-3 天 |
| M4: 多PC + 托盘 | 多 PC 轮询与按键切换；Windows 托盘应用；配置界面 | 完整的多 PC 监控 + Windows 托盘 | 3-4 天 |
| M5: 多平台兼容 | ESP8266 / ESP32-S3 平台适配测试；Linux systemd 服务；ArduinoOTA | 三平台编译通过 + Linux 服务 | 2-3 天 |

建议从 M0 开始，用最小原型验证 ESP + TFT 的横屏绘制能力和串口通信稳定性，确认硬件没问题后再逐步叠加功能。

---

## 附录

### A. 天气图标代码映射 (OpenWeatherMap)

| 代码 | 含义 | 建议图标 |
|------|------|----------|
| 01d / 01n | 晴天 | ☀ / ☾ |
| 02d | 少云 | ⛅ |
| 03d / 04d | 多云 / 阴天 | ☁ |
| 09d / 10d | 小雨 / 雨 | ☔ |
| 11d | 雷暴 | ⛈ |
| 13d | 雪 | ❄ |
| 50d | 雾霾 | ☁ (淡) |

### B. RGB565 常用颜色值

| 颜色 | RGB565 值 | 用途 |
|------|-----------|------|
| 黑色 | 0x0000 | 背景 |
| 白色 | 0xFFFF | 文字 |
| 绿色 | 0x07E0 | 正常状态 |
| 黄色 | 0xFFE0 | 警告状态 |
| 红色 | 0xF800 | 高负载/高温 |
| 蓝色 | 0x001F | 强调 |
| 深灰 | 0x4208 | 进度条背景 |
| 浅灰 | 0xC618 | 次要文字 |

### C. 现有天气时钟项目可复用资源

| 资源 | 说明 | 复用方式 |
|------|------|----------|
| 平台检测宏 | `#if defined(ESP32)` / `#elif defined(ESP8266)` | 直接沿用，扩展 ESP32-S3 分支 |
| TFT_eSprite 绘制模式 | create → fill → draw → push → delete | 作为所有 UI 绘制的标准模式 |
| 进度条 loading() | 启动进度条动画 | 改造为 PCDash 启动画面 |
| 按键检测模式 | digitalRead + 状态变量消抖 | 扩展为四按键 + 长按检测 |
| LED 数字字体 FxLED_32.h | LED 风格大数字 | 直接用于监控数值显示 |
| EEPROM 设置存储 | 串口命令修改 + EEPROM 读写 | 扩展存储项后沿用 |
| WiFi 连接 + SmartConfig | 带超时的 WiFi 连接 + 配网回退 | 直接沿用 |
| NTP 时间同步 | UDP NTP 请求 + TimeLib | WiFi 模式下可选启用作为备用时钟源 |

---

*— 文档结束 —*
