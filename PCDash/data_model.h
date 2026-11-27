#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include "config.h"

// 内存数据
struct MemData {
  float pct;
  float used;    // GB
  float total;   // GB
};

// 网络数据
struct NetData {
  long up;       // bytes/s
  long dn;       // bytes/s
};

// GPU 数据
struct GpuData {
  int usage;     // %
  int temp;      // °C
  int mem_pct;   // %
  int gpuPower;  // W
  char name[24];
};

// 天气数据
struct WthData {
  int temp;      // °C
  int humidity;  // %
  char icon[8];
  char desc[16];
  char city[16];
  char wind[24]; // 风向+风力，如"东北风 3级"（UTF-8 最长约20字节）
};

// 农历数据
struct LunData {
  char gz_y[16]; // 干支年，如"甲辰年"
  char gz_m[16]; // 干支月，如"丁卯月"
  char gz_d[16]; // 干支日，如"壬午日"
  char mo[16];   // 农历月，如"二月"
  char dy[12];   // 农历日，如"廿五"
  char jq[12];   // 节气，如"清明"（非节气日为空）
};

// 单台 PC 的完整监控数据
struct MonitorData {
  char id[16];
  float cpu;
  int cpuTemp;      // °C，-1 表示不可用
  int cpuFreq;      // MHz，0 表示不可用
  int cpuPower;     // W，0 表示不可用
  MemData mem;
  NetData net;
  GpuData gpu;
  bool gpuValid;
  char cpuName[32];    // CPU 型号，如 "i7-12700KF"
  char timeStr[12];    // "HH:MM:SS"（保留用于无 WiFi 时的回退显示）
  char dateStr[12];    // "YYYY-MM-DD"
  char bgHash[9];      // 背景图哈希（来自服务端，空串表示无背景）
  WthData wth;
  bool wthValid;
  LunData lun;
  bool lunValid;
  unsigned long lastUpdate;
  bool connected;
  byte source;         // 0=无, 1=WiFi, 2=Serial
  char sourceIP[16];   // WiFi 来源时记录服务端 IP，用于 DELPC 精准匹配
};

// CC 单个会话详情（来自 /api/status 的 cc.sessions[]）
#define MAX_CC_SESSIONS 6

struct CCSession {
  char id[10];     // session_id 前 8 位
  char state[12];  // alarm/error/busy/thinking/success/off/unknown
};

// CC 聚合状态
struct CCStatus {
  char      state[12];                  // 最高优先级状态
  int       count;                      // 活跃 session 数
  bool      valid;                      // 是否已收到过数据
  CCSession sessions[MAX_CC_SESSIONS];  // session 详情
  int       session_count;              // sessions[] 有效条数
  int       ownerPC;                    // 当前拥有 CC 数据的 pcData 槽位（-1=无主）
};

// 硬件外设声光模式 (MODE:1~5)
enum HardwareMode : uint8_t {
  HW_MODE_LEGACY     = 1,  // 模式1: 现有无声模式 (3色分立LED: 38/39/40, CAM完好)
  HW_MODE_BUZZER     = 2,  // 模式2: 蜂鸣器 (PWM 35) + 3色分立LED (CAM完好)
  HW_MODE_I2S_LEGACY = 3,  // 模式3: MAX98357A I2S (DIN 13) + 3色分立LED (借用CAM_PCLK)
  HW_MODE_I2S_WS2812 = 4,  // 模式4: MAX98357A I2S (DIN 39) + WS2812单线灯 (38) (CAM完好)
  HW_MODE_UART_MP3   = 5   // 模式5: 串口MP3模块 (TX 35, RX 36) + 3色分立LED (CAM完好)
};

// 音频提示类型
enum AudioNotifyType : uint8_t {
  NOTIFY_NONE = 0,
  NOTIFY_SUCCESS,  // 绿灯: 成功/空闲 (叮咚/高频阶梯音)
  NOTIFY_BUSY,     // 黄灯: 工作中/思考中 (短促工作提示音)
  NOTIFY_ALARM     // 红灯: 告警/错误 (急促双嘟音)
};

// 系统运行状态机
enum SystemState : byte {
  STATE_BOOT,
  STATE_WIFI_CONNECTING,
  STATE_WAITING_DATA,
  STATE_RUNNING,
  STATE_DATA_TIMEOUT
};

// 全局应用状态
struct AppState {
  SystemState sysState;
  byte currentPage;
  byte currentPC;
  byte pcCount;
  bool autoScroll;
  uint16_t autoScrollSec;  // 自动轮换间隔（秒），默认 3
  uint8_t  hardwareMode;   // 硬件模式 (1~5，默认 1)
  bool     audioEnabled;   // 全局发声开关 (true=播报, false=静音)
  uint8_t  audioVolume;    // 播报音量 (0~30，默认 25)
  bool darkTheme;
  // byte brightness;  // 硬件无调光电路，已移除
  unsigned long lastPageSwitch;
  unsigned long lastManualSwitch;
  bool forceRedraw;    // 强制全屏重绘标记（切页/主题切换用）
  bool clockTick;      // 时钟秒级刷新（不清屏）
  int  roomTemp;       // DHT11 室温（°C）
  int  roomHumidity;   // DHT11 室内湿度（%）
  bool roomValid;      // DHT11 数据是否有效
};

// FreeRTOS 互斥锁：保护 pcData[] / ccStatus 的跨核并发读写
// 由 PCDash.ino 定义，poll tasks（Core0）写入，主循环（Core1）读取
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
extern SemaphoreHandle_t g_data_mutex;

#endif // DATA_MODEL_H
