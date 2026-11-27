// PCDash — 桌面PC实时监控屏幕 ESP 端固件 (LVGL 版)
// 平台: ESP32-S3 + ILI9341 320×240 + LVGL v9.3

#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <TJpg_Decoder.h>
#include <FFat.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <lvgl.h>
#include <TimeLib.h>

#include "config.h"
#include "data_model.h"
#include "ui/ui.h"
#include "drivers/lv_port_disp.h"
#include "drivers/lv_port_indev.h"
#include "app_binddata.h"

// ui_events.cpp 提供
extern void ui_events_init(void);

// ── 全局对象（lv_port_disp/indev.cpp 通过 extern 访问）──────
TFT_eSPI  tft   = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_CS_PIN, TOUCH_CLK_PIN,
                             TOUCH_DIN_PIN, TOUCH_DO_PIN);

// ── 全局应用数据 ─────────────────────────────────────────────
MonitorData pcData[MAX_PC_COUNT];
AppState    app;
CCStatus    ccStatus;            // CC 状态汇总
SemaphoreHandle_t g_data_mutex;  // 跨核数据互斥锁（Core0 Poll Tasks ↔ Core1 主循环）

// cc_led.ino 提供
extern void cc_led_setup(void);
extern void cc_led_update(void);

// audio_driver.ino 提供
extern void audio_driver_init(void);
extern void audio_driver_notify(AudioNotifyType type);

// data_hub.ino 提供
extern void datahub_checkTimeout(void);
extern void datahub_prevPC(void);
extern void datahub_nextPC(void);

// settings.ino 提供
extern void settings_load(void);
extern void settings_save(void);

// bg_loader.ino 提供
extern void bg_init(void);

// dht_sensor.ino 提供
extern void dht_sensor_setup(void);
extern void dht_sensor_poll(void);

// wifi_client.ino 提供
extern void wifi_init(void);
extern void wifi_poll(void);
extern void wifi_startPollTasks(void);

// serial_recv.ino 提供
extern void serial_receive(void);

// diag.ino 提供
extern void diag_setup(void);
extern void diag_heap_tick(void);
extern void diag_wdt_reset(void);

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300);
    Serial.println("\n========== PCDash LVGL ==========");
    Serial.printf("\n[BUILD] %s %s\n\n", __DATE__, __TIME__);

    EEPROM.begin(256);
    settings_load();
    g_data_mutex = xSemaphoreCreateMutex();  // 必须在任何 Task 启动前创建
    bg_init();    // 挂载 FFat（背景图缓存）

    // 应用状态初始化
    app.sysState         = STATE_BOOT;
    app.currentPage      = 0;
    app.currentPC        = 0;
    app.pcCount          = 0;
    if (app.autoScrollSec == 0) app.autoScrollSec = 3;
    app.lastPageSwitch   = millis();
    app.lastManualSwitch = 0;
    app.forceRedraw      = false;
    app.clockTick        = false;
    for (int i = 0; i < MAX_PC_COUNT; i++) {
        memset(&pcData[i], 0, sizeof(MonitorData));
        strcpy(pcData[i].id, "---");
    }
    memset(&ccStatus, 0, sizeof(CCStatus));
    strcpy(ccStatus.state, "off");
    ccStatus.ownerPC = -1;

    // LVGL 初始化（顺序不能乱）
    lv_init();
    lv_port_disp_init();     // TFT 初始化 + 帧缓冲注册 + 背光
    lv_port_indev_init();    // 触摸屏校准 + 按键 GPIO 注册

    // SquareLine Studio 导出 UI 初始化
    ui_init();

    // 数据绑定初始化（写静态文本、清零进度条）
    app_binddata_init();

    // 注册手势 / 按键事件回调
    ui_events_init();

    // 音频驱动与 CC 状态 LED 初始化
    audio_driver_init();
    cc_led_setup();

    // DHT11 室内温湿度
    dht_sensor_setup();

    // WiFi / NTP
    wifi_init();
    wifi_startPollTasks();   // 为每台 PC 启动独立 Poll Task（Core0）
    app.sysState = STATE_WAITING_DATA;

    Serial.printf("[SETUP] 空闲堆: %d bytes\n", ESP.getFreeHeap());
    Serial.println("[SETUP] 就绪");

    // 诊断系统（必须在最后初始化）
    diag_setup();
}

// ════════════════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    // ── LVGL tick（必须在 lv_timer_handler 之前）────────────
    static uint32_t last_lvgl_tick = 0;
    lv_tick_inc((uint32_t)(now - last_lvgl_tick));
    last_lvgl_tick = (uint32_t)now;

    // ── 数据接收 & 网络 ──────────────────────────────────────
    serial_receive();
    wifi_poll();
    datahub_checkTimeout();
    cc_led_update();
    dht_sensor_poll();

    // ── 设备自动轮换 ─────────────────────────────────────────
    if (app.autoScroll && app.sysState == STATE_RUNNING) {
        uint32_t scrollInterval = (app.autoScrollSec > 0 ? app.autoScrollSec : 3) * 1000UL;
        if (now - app.lastPageSwitch  >= scrollInterval &&
            now - app.lastManualSwitch >= scrollInterval) {
            datahub_nextPC();
            app.lastPageSwitch = now;
        }
    }

    // ── UI 刷新（200ms 节拍，LVGL 脏区渲染，低CPU开销）──────
    static unsigned long last_bind = 0;
    if (now - last_bind >= 200) {
        last_bind = now;
        app_binddata_update();
    }

    // ── 时钟每秒单独刷新（仅更新 lbltime，不触发全量绑定）──
    static unsigned long last_clock = 0;
    if (now - last_clock >= 1000) {
        last_clock = now;
        app_binddata_tick_clock();
    }

    // ── LVGL 事件处理 + 增量渲染 ────────────────────────────
    lv_timer_handler();

    // ── 原始触摸滑动切换设备 ─────────────────────────────────
    int8_t swipe = lv_port_indev_consume_swipe();
    if (swipe != 0) {
        if (swipe < 0) datahub_nextPC();   // 左滑 → 下一台
        else           datahub_prevPC();   // 右滑 → 上一台
        app.lastManualSwitch = millis();
    }

    // ── 安全守护 ─────────────────────────────────────────────
    diag_heap_tick();
    diag_wdt_reset();
}
