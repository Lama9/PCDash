# PCDash — 桌面 PC 实时监控屏幕

> **变更记录**  
> 日期：2026-11-19  
> 修改模型：Gemini 3.8 Flash  

基于 ESP 系列 MCU + TFT 彩色显示屏的桌面硬件监控小组件。通过 WiFi 或 USB 串口与 PC 端 Python 服务通信，实时展示 CPU、内存、磁盘、网络、GPU、时钟、农历和天气信息。

---

## 功能特性

- **全面的硬件监控：** CPU 使用率、温度、主频、功率、**型号**；内存使用率；网络上传/下载速度；NVIDIA GPU 使用率、温度、显存、功率、**型号**
- **LVGL v9.3 UI 框架：** 基于 LVGL + SquareLine Studio 1.6.1 构建声明式 UI，单页双面板布局（上半 CPU+GPU，下半时钟+天气+农历）
- **动态进度条颜色：** 使用率/功率 60%/90% 变色；CPU 温度 70°C/90°C；GPU 温度 70°C/85°C（绿→黄→红）
- **天气完整显示：** 城市、天气描述、温度、湿度、**风向风力** 各自独立控件；支持 OpenWeatherMap / Open-Meteo / 和风天气三渠道
- **双通道通信：** WiFi（ESP 轮询 PC HTTP 接口）和 USB 串口（PC 主动推送）同时工作，互不干扰，串口数据优先
- **多 PC 支持：** WiFi 模式下最多同时连接 3 台 PC，触摸/按键切换显示
- **实时时钟：** WiFi 模式下 ESP 自主通过 NTP 同步时间（每 30 分钟），**无 WiFi 时自动回退到 PC 串口下发时间**
- **完整农历信息：** PC 端离线计算干支年月日、农历月日、节气，无需 API
- **Claude Code 状态感知：** 通过 CC Hooks 实时捕获 Claude Code 工作状态（thinking / busy / alarm / error / success / idle / off），三色 LED（GPIO38 红 / GPIO39 黄 / GPIO40 绿）+ 屏幕 CC 状态按钮 + 滑动详情页面联动展示；支持最多 6 个 CC 会话并发，按优先级聚合；LED 颜色：红=alarm/error，黄=busy/thinking，绿=success/idle
- **Windows 托盘运行：** PC 端后台静默运行，任务栏图标管理
- **Linux systemd 服务：** Linux 平台开机自启服务

---

## 系统架构

```
┌─────────────────────┐     WiFi (HTTP GET)      ┌──────────────────────────┐
│  PC-A Python Service │ ◄────────────────────────┤                          │
└─────────────────────┘                           │   ESP8266 / ESP32        │
┌─────────────────────┐     WiFi (HTTP GET)       │   / ESP32-S3             │
│  PC-B Python Service │ ◄────────────────────────┤                          │
└─────────────────────┘                           │   + ST7735 / ILI9341 TFT │
┌─────────────────────┐     USB Serial (Push)     │   + K1-K3 三按键         │
│  PC-C Python Service │ ────────────────────────►│   + 三色 CC LED(R/Y/G)   │
                                                   │   + 触摸屏（可选）       │
└─────────────────────┘                           └──────────────────────────┘
```

PC 端 Python 服务负责采集数据并通过双通道下发；ESP 端负责解析数据并驱动 TFT 显示。

---

## 硬件需求

| 组件 | 推荐规格 | 备注 |
|------|---------|------|
| MCU | ESP32 | 首选；ESP8266、ESP32-S3 同样支持 |
| 显示屏 | ILI9341 2.8寸 320×240 | 或 ST7735 1.8寸 160×128 |
| 按键 | K1-K3 三按键 | 屏幕模块板载（K4 引脚已复用为 CC LED） |
| CC LED | 三色 LED（GPIO38 红 / GPIO39 黄 / GPIO40 绿） | 共阴接法，直接驱动或 PWM |
| 触摸屏 | XPT2046（可选） | ILI9341 扩展板支持 |
| 连接方式 | USB 串口 / WiFi 局域网 | 两种方式可同时使用 |

### 硬件购买参考

| 硬件组件 | 购买链接 | 说明 |
| :--- | :--- | :--- |
| **ESP32-S3 主板** | [天猫购买链接](https://detail.tmall.com/item.htm?id=718360850411) | ESP32-S3 核心板 |
| **底板** | [天猫购买链接](https://detail.tmall.com/item.htm?id=718360850411) | 核心板配套扩展底板 |
| **TFT 屏幕** | [淘宝购买链接](https://item.taobao.com/item.htm?id=838408412725) | 推荐的显示屏幕 |
| **红绿灯模块** | [天猫购买链接](https://detail.tmall.com/item.htm?id=672517249922) | 三色 CC LED 指示灯模块 |

---

## 监控数据项

| 数据项 | 刷新频率 | 说明 |
|--------|----------|------|
| CPU 使用率 | 2-3 秒 | 百分比，精度 0.1% |
| CPU 型号 | 启动时一次 | 如"i7-12700KF"，缓存后随每帧下发 |
| CPU 温度 | 2-3 秒 | Windows 需 LHM Web Server；不可用时显示 N/A |
| CPU 主频 | 2-3 秒 | MHz 整数 |
| CPU 功率 | 2-3 秒 | Windows: LHM；Linux: Intel RAPL；其他显示 N/A |
| 内存使用率 | 2-3 秒 | 百分比 + 已用/总量 (GB) |
| 网络速度 | 2-3 秒 | 上传 / 下载 (bytes/s) |
| GPU 使用率 / 温度 / 显存 / 功率 / 型号 | 2-3 秒 | 仅支持 NVIDIA GPU；无 GPU 时显示 N/A |
| 实时时钟 | 每秒刷新 | ESP NTP 自主维护；无 WiFi 时自动回退 PC 串口时间 |
| 农历 | 每次推送 | 干支年月日 + 农历月日 + 节气 |
| 天气 | 30 分钟 | 城市 + 描述 + 温度 + 湿度 + 风向风力 |

---

## UI 布局

采用单页双面板设计（LVGL v9.3 + SquareLine Studio）：

```
┌──────────────────────────────────────────────────────┐
│  状态栏：[↑上传] [↓下载]  [设备名]  [连接类型]  [CC●]  │
├──────────────────────┬───────────────────────────────┤
│  CPU 面板            │  GPU 面板                      │
│  [CPU型号]           │  [GPU型号]                     │
│  USG ████░░  绿/黄/红 │  USG ████░░  绿/黄/红         │
│  TMP ███░░░          │  TMP ████░░                    │
│  PWR ██░░░░          │  PWR █████░                    │
│  MEM ███░░░          │  MEM ██░░░░                    │
├──────────────────────┴───────────────────────────────┤
│  14:30              2026-06-14                        │
│  [城市] [天气] [28°C] [65%] [东北风 3级]              │
│  四月廿五   甲辰年                    芒种             │
│                   ● ○ ○                               │
└──────────────────────────────────────────────────────┘
```

触摸左右滑动或按键切换设备，底部圆点指示当前显示的 PC。

**按键操作：**

| 操作 | 短按 / 触摸 | 长按 (>1.5s) |
|------|------------|-------------|
| K1 | 上一设备 | 自动轮播开/关 |
| K2 | 下一设备 | 切换亮/暗主题 |
| K3 | 切换 PC | 进入设置页 |
| 触摸 CC 按钮 | 打开 CC 状态详情页（滑动返回） | — |

> K4 引脚（GPIO38）已复用为三色 CC LED 红色通道，按键功能已移除。

---

## PC 端安装与运行

### 1. 安装依赖

```powershell
cd X:\esp\PCDash\pcdash-server
pip install -r requirements.txt
```

依赖库清单：

| 库 | 用途 |
|----|------|
| psutil | CPU / 内存 / 磁盘 / 网络采集 |
| nvidia-ml-py | NVIDIA GPU 数据采集 |
| flask | HTTP 服务（供 ESP 轮询） |
| pyserial | USB 串口通信 |
| requests | 天气 API 及 IP 定位 |
| sxtwl | 农历离线计算（干支、节气） |
| pystray | Windows 系统托盘图标 |
| Pillow | 托盘图标生成 |
| PyYAML | YAML 配置文件解析 |

### 2. 启动服务

**Windows 托盘模式（推荐）：**

```powershell
python pcdash-server\main.py
```

启动后 PC 右下角出现托盘图标，右键可打开配置、查看状态或退出。

**无界面模式（Linux / 命令行）：**

```powershell
python pcdash-server\main.py --headless
```

**Linux systemd 服务：**

```bash
# 先编辑 pcdash-server/service/pcdash.service，把 <username> 替换成实际运行用户名
# （默认假定部署路径为 /home/<username>/pcdash-server，含 venv）
sudo cp pcdash-server/service/pcdash.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable pcdash
sudo systemctl start pcdash
```

> **CPU 功率（`cpu_w`）在 Linux 上一直是 `null`？** 服务以普通用户运行时，内核默认禁止非 root 读取 RAPL 功率
> sysfs 文件（Platypus 侧信道漏洞修复导致），需要额外授权一次（sysfs 不支持 ACL，用 chgrp+chmod）：
> ```bash
> # 编辑 pcdash-server/service/99-rapl-pcdash.rules，把 <username> 换成实际运行服务的 Linux 用户名
> sudo cp pcdash-server/service/99-rapl-pcdash.rules /etc/udev/rules.d/
> sudo udevadm control --reload-rules
> sudo udevadm trigger -s powercap
> ```

**手工验证服务是否正常运行：**

```bash
# 1. 服务当前是否在跑
systemctl status pcdash

# 2. 是否真的注册了开机自启（权威判断，不要只看 status 里的 enabled 字样）
systemctl is-enabled pcdash

# 3. 看日志，确认没有报错/异常重启
journalctl -u pcdash -n 50 --no-pager
journalctl -u pcdash -f          # 实时跟随，Ctrl+C 退出

# 4. 功能级验证：直接打 API，看数据是否正常（尤其 cpu_w 是否还是 null）
curl -s http://127.0.0.1:18266/api/status | python3 -m json.tool
```

最硬核的验证是重启一次服务器，确认服务自动拉起（`sudo reboot` 后 `systemctl status pcdash` 应为 `active (running)`），但平时不必为了验证专门重启，`is-enabled` + `status` 已足够可靠地证明"配置正确，下次开机会自启"。

### 3. 配置文件

首次运行后会在用户目录生成 `pcdash_config.yaml`，也可直接编辑 `pcdash-server/config/default_config.yaml`：

```yaml
device_name: ""              # 留空则自动使用系统主机名；也可手动指定

serial:
  enabled: true
  port: auto                 # auto = 自动识别 CH340/CP2104；或手动填 "COM3"
  baud: 115200
  interval: 2                # 数据推送间隔（秒）

wifi:
  enabled: true
  host: 0.0.0.0
  port: 8266                 # ESP 通过此端口 HTTP 轮询

gpu:
  enabled: true
  device_index: 0

weather:
  enabled: true
  api_key: ""                # 留空 → 自动使用免费 Open-Meteo
                             # 填入 OWM Key → 优先使用 OpenWeatherMap
  location: auto             # auto = IP 自动定位
  # lat: 39.9042             # 手动覆盖经纬度（可选）
  # lon: 116.4074
  interval: 1800

disk:
  path: "C:\\"               # Linux 填 "/"
```

### 4. Windows CPU 温度/功率配置（可选）

Windows 下需借助 [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases) 读取 CPU 温度和功率，**无需管理员权限，无需额外 Python 包**：

1. 下载最新版 LibreHardwareMonitor，解压到任意目录
2. 运行 `LibreHardwareMonitor.exe`
3. 菜单 `Options → Start web server` ✓（启用内置 Web Server，默认端口 8085）
4. 菜单 `Options → Run On Windows Startup` ✓（推荐开机自启）
5. 菜单 `Options → Start Minimized` ✓

> LHM 未运行或 Web Server 未启用时，`cpu_t`/`cpu_w` 自动返回 `null`，其余功能不受影响。

---

## ESP 端烧录

### 开发环境

- **IDE：** Arduino IDE 2.x
- **核心依赖库：** TFT_eSPI、ArduinoJson、WiFi/ESP8266WiFi、HTTPClient/ESP8266HTTPClient、TimeLib

### 编译配置

在 `PCDash/config.h` 中修改以下宏后编译烧录：

```cpp
// ======== 屏幕选择（二选一）========
// #define USE_ST7735      // 1.8寸 ST7735 160×128
#define USE_ILI9341        // 2.8寸 ILI9341 320×240（当前默认）

// ======== ESP32-S3 原生 USB CDC ========
#define USE_CDC_SERIAL  0  // 1 = 使用 USBSerial（S3 原生 USB）；0 = 使用 UART Serial
```

按需填写 WiFi 凭据和 PC IP（也可通过串口命令配置）：

```cpp
// WiFi 配置
// 可在 settings.ino 中或通过串口命令 WIFI:ssid,password 配置
```

### 串口配置命令

通过串口向 ESP 发送配置或诊断命令（波特率 115200，以换行符 `\n` 结尾；PC 端工具或串口终端均可）：

| 命令 | 格式 | 示例 | 说明 |
|------|------|------|------|
| **WIFI** | `WIFI:ssid,password` | `WIFI:MyWiFi,12345678` | 配置 WiFi SSID 与密码（各最长 32 字节），写入 EEPROM 持久化 |
| **PCIP** | `PCIP:index,ip` | `PCIP:0,192.168.1.100` | 配置指定槽位（`0~2`）的 PC IP 地址，写入 EEPROM；同时重置该槽位端口为默认值 (18266) |
| **PORT** | `PORT:index,port` | `PORT:0,18266` | 配置指定槽位（`0~2`）的 HTTP 轮询端口（默认 18266），支持各 PC 独立配置端口 |
| **DELPC** | `DELPC:index` | `DELPC:0` | 删除指定槽位（`0~已配置数-1`）的 PC 配置，清除 IP/端口并自动前移后续槽位，重启轮询 Task |
| **STATUS** | `STATUS` | `STATUS` | 查询并打印系统全量运行状态（不区分大小写），含运行时长、WiFi、NTP、EEPROM 槽位、运行时设备列表、DHT11 温湿度、内存占用等 |
| **PREV** | `PREV` 或 `PAGE:PREV` | `PREV` | 翻到上一页（上一台设备），重置自动轮播计时，刷新屏幕 |
| **NEXT** | `NEXT` 或 `PAGE:NEXT` | `NEXT` | 翻到下一页（下一台设备），重置自动轮播计时，刷新屏幕 |
| **AUTOSCROLL** | `AUTOSCROLL:1` 或 `0` | `AUTOSCROLL:1` | 开启（`1`/`ON`）或关闭（`0`/`OFF`）设备自动轮播，写入 EEPROM 持久化 |
| **SCROLLTIME** | `SCROLLTIME:sec` | `SCROLLTIME:5` | 设置设备自动轮播时间间隔（秒，默认 3，范围 1~3600），写入 EEPROM 持久化 |
| **MODE** | `MODE:1~5` | `MODE:2` | 切换硬件外设声光模式（1:无声, 2:蜂鸣器, 3:I2S+3色灯, 4:I2S+单线灯, 5:串口MP3），写入 EEPROM 持久化 |
| **AUDIO** | `AUDIO:1` 或 `0` | `AUDIO:1` | 开启（`1`/`ON`）或关闭（`0`/`OFF`/静音）语音与声音播报，写入 EEPROM 持久化 |
| **VOLUME** | `VOLUME:0~30` 或 `VOL:` | `VOL:25` | 设置系统播报音量（0~30级，默认 25），写入 EEPROM 持久化并即时下发生效 |
| **PLAY** | `PLAY:1~3` | `PLAY:1` | 手动点播测试提示音（1:成功, 2:工作中, 3:告警3轮） |
| **TEST:MP3** | `TEST:MP3` | `TEST:MP3` | CX-SP5T 模块专属硬件双向通信诊断与抓包测试 |
| **HELP** | `HELP` | `HELP` | 在串口打印所有配置命令的详细使用帮助（只打印说明，不修改配置） |

> **说明：**
> - 若串口接收到以 `{` 开头的内容（如 `{"id":"My-PC", ...}\n`），ESP 会自动识别为硬件监控 JSON 数据帧直接解析并刷新屏幕（USB 串口直推通道）。
> - 配置命令格式要求严格匹配冒号与英文逗号分隔，配置成功后串口会返回 `[CMD] ... OK` 确认信息。

### 硬件声光模式与引脚映射 (MODE:1~5)

PCDash 支持 5 种硬件工作模式，通过串口发送 `MODE:1` ~ `MODE:5` 即时切换并保存至 EEPROM：

| 模式 | 模式名称 | 声音输出与引脚 | 状态指示灯与引脚 | CAM_PCLK (GPIO13) 状态 | 适用场景 |
|:---:|:---|:---|:---|:---:|:---|
| **1** | 现有无声模式 | 无（静音） | 分立 3 色 LED (R=38, Y=39, G=40) | **未占用** (摄像头完好) | 基础配置，无声监控 |
| **2** | 蜂鸣器模式 | 无源蜂鸣器 (PWM: GPIO35) | 分立 3 色 LED (R=38, Y=39, G=40) | **未占用** (摄像头完好) | 轻量音效提示，无需占用引脚 |
| **3** | I2S 功放模式 | MAX98357A (BCLK=35, LRC=36, DIN=13) | 分立 3 色 LED (R=38, Y=39, G=40) | **已占用** (暂不可用摄像头) | 现有 3 色灯基础上获得功放音质 |
| **4** | I2S 功放+单线灯 | MAX98357A (BCLK=35, LRC=36, DIN=39) | WS2812B 单线灯 (DIN=38) | **未占用** (摄像头与I2S并存) | 最佳综合方案：功放与摄像头完美共存 |
| **5** | 串口 MP3 模式 | CX-SP5T (UART2: TX=35, RX=36 双向) | 分立 3 色 LED (R=38, Y=39, G=40) | **未占用** (摄像头完好) | 模块内置 TF 卡播放自定义 MP3 (单曲停止模式) |

> **提示音联动与全局静音：**
> - **绿灯（完成/空闲）：** 触发成功音（模式 2/3/4 双音叮咚；模式 5 播放根目录 `00001.mp3`）
> - **黄灯（工作中/思考中）：** 触发工作音（短声提示；模式 5 播放根目录 `00002.mp3`）
> - **红灯（告警/异常）：** 触发告警音（急促双嘟循环 3 轮；模式 5 播放根目录 `00003.mp3`）
> - **全局开关与音量：** 发送 `AUDIO:0` 可一键全系统静音；发送 `VOLUME:0~30`（或 `VOL:20`）统一调节播报音量。
> - **TF卡文件存放：** 根目录下放置 `00001.mp3`、`00002.mp3`、`00003.mp3`（样例位于 `docs/audio_mp3/` 目录）。模块强制初始化为单曲播放停止模式，每次播完即停。

---

## 通信协议

WiFi 和 USB 串口使用统一 JSON 格式，示例：

```json
{
  "id":       "My-PC",
  "cpu":      45.2,
  "cpu_name": "i7-12700KF",
  "cpu_t":    65,
  "cpu_f": 3600,
  "cpu_w": 95,
  "mem":   { "pct": 51.2, "used": 16.4, "total": 32.0 },
  "dsk":   { "pct": 50.0, "used": 125,  "total": 446 },
  "net":   { "up": 14832, "dn": 5007 },
  "gpu":   { "usage": 78, "temp": 65, "mem_pct": 42, "name": "RTX 3080", "watt": 220 },
  "time":  "14:30:25",
  "date":  "2026-06-10",
  "lun":   { "gz_y": "丙午年", "gz_m": "壬午月", "gz_d": "甲子日",
             "mo": "四月", "dy": "廿五", "jq": "芒种" },
  "wth":   { "temp": 28, "humidity": 65, "icon": "02d",
             "desc": "晴", "city": "北京", "wind": "东北风 3级" },
  "cc":    { "state": "busy", "count": 2,
             "sessions": [{"id": "abc12345", "state": "busy"},
                          {"id": "def67890", "state": "thinking"}] }
}
```

- **WiFi 接口：** `http://<pc-ip>:8266/api/status`（ESP 每 3 秒轮询；响应中已内嵌 `cc` 字段）
- **CC 状态上报：** CC Hook 脚本 `POST /api/cc-status`（仅 Python 本机调用；ESP 不单独轮询此接口）
- **串口波特率：** 115200，每条 JSON 以 `\n` 结尾，PC 每 2 秒推送一次
- **字段为 `null` 时：** ESP 端显示 "N/A"

详细协议规范见 [`doc/PCDash_PRD_v1.3.md`](doc/PCDash_PRD_v1.3.md)。

---

## 工程结构

```
PCDash/                         # ESP 端 Arduino 工程 (ESP32-S3 + LVGL v9.3)
├── PCDash.ino                  # 主入口 setup() / loop()
├── config.h                    # GPIO 定义、参数常量
├── data_model.h                # 数据结构体 (MonitorData / AppState)
├── lv_conf.h                   # LVGL 配置（项目本地，覆盖系统库）
├── app_binddata.cpp            # 数据绑定层：MonitorData / CCStatus → LVGL 控件
├── wifi_client.ino             # WiFi 连接与 HTTP 轮询（/api/status 含 cc 字段）
├── serial_recv.ino             # 串口接收与 JSON 解析（含 CC 字段解析）
├── data_hub.ino                # 双通道数据合并（Serial 优先）
├── cc_led.ino                  # CC 状态三色 LED 驱动（GPIO38/39/40）
├── cc_screen.cpp               # CC 状态详情页（B 方案：骨架/数据分离，热刷新）
├── ui_events.cpp               # LVGL 事件绑定（CC 按钮 → 详情页，触摸调试）
├── buttons.ino                 # 按键扫描与长/短按判定
├── touch.ino                   # 触摸屏手势检测（XPT2046，180° 坐标修正）
├── ntp.ino                     # NTP 时间同步
├── settings.ino                # EEPROM 配置持久化
├── ui_src.cpp                  # 桥接：#include ui/*.c
├── font_fangsong12/14.cpp      # 桥接：各自独立翻译单元（防静态变量冲突）
├── font_yahei14.cpp
├── drivers_src.cpp             # 桥接：lv_port_disp + lv_port_indev
├── patch_ui.ps1                # SquareLine 导出后 C++ 兼容性补丁脚本
├── ui/                         # SquareLine Studio 导出文件（勿手动修改）
│   ├── ui_scrmain.h / ui_scrmain.c
│   ├── ui_helpers.c / ui_comp.c ...
│   └── ui_font_*.c
└── drivers/
    ├── lv_port_disp.cpp        # LVGL flush → TFT_eSPI
    └── lv_port_indev.cpp       # LVGL touch → TFT_Touch

pcdash-server/                  # PC 端 Python 服务
├── main.py                     # 入口点
├── collector/
│   ├── system_info.py          # CPU / 内存 / 网络 + 温度/主频/功率/型号
│   ├── gpu_info.py             # NVIDIA GPU (pynvml)
│   ├── weather_info.py         # 天气 + 风向风力 (三渠道)
│   └── lunar_info.py           # 农历离线计算 (sxtwl)
├── cc_hook.py                  # CC Hooks 脚本（精确版）：读 stdin payload → POST /api/cc-status
├── transport/
│   ├── http_server.py          # Flask HTTP 服务（/api/status 内嵌 cc 字段；/api/cc-status 供 Hook 上报）
│   └── serial_sender.py        # 串口推送 + 自动重连
├── config/
│   └── default_config.yaml
├── ui/                         # Windows 托盘与配置界面
├── service/
│   └── pcdash.service          # Linux systemd 服务文件
└── requirements.txt

doc/
├── PCDash_PRD_v1.2.md          # PRD v1.2
└── PCDash_PRD_v1.3.md          # PRD v1.3（当前版本）

docs/
├── LVGL_v9_移植记录.md
└── LVGL_学习指南.md
```

---

## 常见问题

**Q: 天气显示 N/A？**  
A: 检查网络连接。无需配置 API Key，服务会自动使用免费 Open-Meteo。若有防火墙限制，确保 PC 可访问 `api.open-meteo.com` 和 `ip-api.com`。

**Q: CPU 温度显示 N/A（Windows）？**  
A: 需要运行 LibreHardwareMonitor 并在 `Options → Start web server` 中启用 Web Server。详见上方"Windows CPU 温度/功率配置"章节。

**Q: 串口无法连接？**  
A: 将 `config.yaml` 中 `serial.port` 从 `auto` 改为具体端口号（如 `COM3`）。设备管理器中确认 ESP 对应的 COM 口编号。

**Q: ESP32-S3 无法接收数据？**  
A: S3 使用原生 USB CDC 时，在 `config.h` 中设置 `USE_CDC_SERIAL 1`。

**Q: 多台 PC 如何切换显示？**  
A: WiFi 模式下，K3 短按切换到下一台 PC。需提前通过串口命令 `PCIP:0,<ip>` / `PCIP:1,<ip>` 配置各 PC 地址。

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| v1.4 | 2026-06-15 | Claude Code 状态感知：cc_hook.py + /api/cc-status 上报 → /api/status 嵌入 cc 字段 → ESP 解析；三色 CC LED（GPIO38/39/40）；CC 详情页（B 方案滚动列表）；触摸 180° 坐标修正；K4 引脚复用为 CC LED |
| v1.3 | 2026-06-14 | UI 全面迁移 LVGL v9.3；单页双面板；天气拆四控件+风力风向；CPU/GPU型号标题；进度条动态颜色；NTP串口回退 |
| v1.2 | 2026-06-10 | 新增 CPU 温度/主频/农历；天气 API 增加 Open-Meteo 降级；ESP 独立 NTP 授时 |
| v1.1 | — | 新增天气显示；双通道通信；多 PC 支持 |
| v1.0 | — | 初始版本；串口通信；CPU/内存/磁盘/网络/GPU 基础监控 |

---

## 版权与许可

Copyright © 2024-2026 hongyongg (<hongyongg@gmail.com>)

本项目以 **MIT License** 开源发布，允许自由使用、修改和分发，保留版权声明即可。

本项目参考了以下开源项目的实现思路，在此致谢：

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — ESP 系列 TFT 驱动库
- [ArduinoJson](https://arduinojson.org/) — ESP 端 JSON 解析
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) — Windows 硬件传感器访问
- [sxtwl](https://github.com/yuangu/sxtwl_cpp) — 农历干支节气计算
- [Open-Meteo](https://open-meteo.com/) — 免费天气 API
