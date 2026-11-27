#ifndef CONFIG_H
#define CONFIG_H

// ======== 平台：仅支持 ESP32-S3 ========
#include <WiFi.h>
#include <HTTPClient.h>
#define PLATFORM_NAME    "ESP32-S3"
#define HAS_PSRAM        1
#define HAS_NATIVE_USB   1

// ======== 屏幕：ILI9341 320×240 ========
#define SCREEN_W         320
#define SCREEN_H         240
#define SCREEN_ROTATION  3
#define INVERT_DISPLAY   0

// ======== GPIO（ESP32-S3-CAM + EXB 扩展板）========
// TFT 背光（PWM）
#define TFT_BL_PIN     3

// DHT11 室内温湿度传感器（GPIO37，通过 P8 跳帽接入）
#define DHT11_PIN          37
#define DHT11_INTERVAL_MS  10000   // 采样间隔（毫秒）

// CC 状态 LED（共阴接法，GPIO HIGH = 亮）
#define CC_LED_RED_PIN  38   // 红 = error / alarm
#define CC_LED_YLW_PIN  39   // 黄 = busy / thinking
#define CC_LED_GRN_PIN  40   // 绿 = success

// ======== 音频与硬件模式引脚定义 ========
#define PIN_BUZZER_PWM    35   // 模式 2: 无源蜂鸣器引脚 (排针 P6)
#define PIN_I2S_BCLK      35   // 模式 3/4: I2S 位时钟 (排针 P6)
#define PIN_I2S_LRC       36   // 模式 3/4: I2S 左右声道时钟 (排针 P6)
#define PIN_I2S_DIN_M3    13   // 模式 3: I2S 数据输入 (借用 CAM_PCLK)
#define PIN_I2S_DIN_M4    39   // 模式 4: I2S 数据输入 (占用原黄色LED引脚)
#define PIN_WS2812_DIN    38   // 模式 4: WS2812B 单线灯数据引脚 (占用原红色LED引脚)
#define PIN_MP3_TX        35   // 模式 5: 串口 MP3 TX (连接到模块 RX)
#define PIN_MP3_RX        36   // 模式 5: 串口 MP3 RX (连接到模块 TX)

// 触摸屏引脚（T_CS / T_CLK / T_DIN / T_DO）
#define TOUCH_CS_PIN   1
#define TOUCH_CLK_PIN  42
#define TOUCH_DIN_PIN  2
#define TOUCH_DO_PIN   41

// 串口选择: 0=CH340/UART0, 1=原生USB CDC
#define USE_CDC_SERIAL 0

// 数据通信串口（与 PC 端通信）
#if USE_CDC_SERIAL
  #define DATA_SERIAL  USBSerial
#else
  #define DATA_SERIAL  Serial
#endif

// ======== NTP 时间同步 ========
#define NTP_SERVER         "ntp6.aliyun.com"
#define NTP_LOCAL_PORT     8000
#define NTP_TIMEZONE       8       // 东八区 UTC+8
#define NTP_SYNC_INTERVAL  1800    // 同步间隔（秒），30 分钟

// ======== 触摸滑动阈值 ========
#define SWIPE_MIN_DIST  40   // 最小滑动距离（像素）
#define SWIPE_MAX_MS    600  // 最大滑动时间（毫秒）

// ======== 通用常量 ========
#define SERIAL_BAUD       115200
#define JSON_BUF_SIZE     1024    // ArduinoJson 缓冲区（含农历UTF-8字段）
#define POLL_INTERVAL_MS  1000    // WiFi HTTP 轮询间隔（每台PC独立1s）
#define DATA_TIMEOUT_MS   30000   // 数据超时判定（30秒）
#define PAGE_COUNT        1       // 总页数（单页设计）
#define AUTO_SCROLL_MS    3000    // 设备自动切换间隔（毫秒）
#define DEBOUNCE_MS       50      // 按键消抖
#define LONG_PRESS_MS     1500    // 长按判定
#define MAX_PC_COUNT      3       // 最多监控 PC 数量
#define WIFI_TIMEOUT_MS   15000   // WiFi 连接超时
#define HTTP_TIMEOUT_MS   800     // HTTP 请求超时（缩短以快速失败）
#define POLL_TASK_STACK   8192    // Poll Task 栈大小（含 HTTPClient + JSON 解析）
#define DEFAULT_HTTP_PORT 18266

// ======== 背光 ========
// 硬件无调光电路（GPIO0 strapping pin），背光始终常亮

// ======== UI 布局常量 ========
#define UPPER_Y       16    // 上半区起始 Y
#define UPPER_H       112   // 上半区高度（含标题栏）
#define LOWER_Y       128   // 下半区起始 Y
#define LOWER_H       104   // 下半区高度（留 8px 给底部分页点）
#define DOT_H         8     // 底部分页指示点条高度
#define ROW_H         24    // 上半区单行高度（(UPPER_H-14)/4=24.5→24）
#define PANEL_W       160   // 单面板宽度（SCREEN_W/2）

// ======== 背景图片 ========
#define BG_SPIFFS_PATH      "/bg.jpg"   // SPIFFS 存储路径
#define BG_DOWNLOAD_TIMEOUT 10000       // 下载超时（毫秒）
#define EEPROM_BGHASH_ADDR  131         // 背景图哈希（8 字节，131-138）
// 用于精灵透明叠加的魔法色（不会出现在正常 UI 中）
#define UI_TRANSPARENT      0x0120

// ======== 颜色定义 (RGB565) ========
#define CLR_BLACK    0x0000
#define CLR_WHITE    0xFFFF
#define CLR_GREEN    0x07E0
#define CLR_YELLOW   0xFFE0
#define CLR_RED      0xF800
#define CLR_BLUE     0x001F
#define CLR_CYAN     0x07FF
#define CLR_ORANGE   0xFD20
#define CLR_DGRAY    0x4208   // 深灰（进度条背景）
#define CLR_LGRAY    0xC618   // 浅灰（次要文字）
#define CLR_DKBLUE   0x000B   // 深蓝（状态栏背景）

#endif // CONFIG_H
