// cc_led.ino — CC 状态 LED 控制（支持分立 3 色 LED 与 WS2812 单线灯，状态跳变触发声音通知）

#include "config.h"
#include "data_model.h"

extern CCStatus ccStatus;
extern AppState app;
extern void audio_driver_notify(AudioNotifyType type);

#define LED_FULL  180   // PWM 满亮（0-255），非255防止直视刺眼
#define LED_OFF     0

static unsigned long _ledTick = 0;
static bool          _ledOn   = false;
static char          _last_state[12] = "";

static void _led_set(int r, int y, int g) {
    if (app.hardwareMode == HW_MODE_I2S_WS2812) {
        // 模式 4: WS2812B 单线灯 (DIN=38)
        if (r > 0 && y == 0 && g == 0) {
            rgbLedWrite(PIN_WS2812_DIN, r, 0, 0);
        } else if (y > 0 && r == 0 && g == 0) {
            // 黄光调配: 红+绿
            rgbLedWrite(PIN_WS2812_DIN, y, (uint8_t)(y * 0.75f), 0);
        } else if (g > 0 && r == 0 && y == 0) {
            rgbLedWrite(PIN_WS2812_DIN, 0, g, 0);
        } else {
            rgbLedWrite(PIN_WS2812_DIN, 0, 0, 0);
        }
    } else {
        // 模式 1/2/3/5: 分立 3 色 LED (R=38, Y=39, G=40)
        analogWrite(CC_LED_RED_PIN, r);
        analogWrite(CC_LED_YLW_PIN, y);
        analogWrite(CC_LED_GRN_PIN, g);
    }
}

void cc_led_setup() {
    if (app.hardwareMode == HW_MODE_I2S_WS2812) {
        // 模式 4: 释放 GPIO39(供 I2S DIN 使用) 和 GPIO40
        pinMode(CC_LED_YLW_PIN, INPUT);
        pinMode(CC_LED_GRN_PIN, INPUT);
        rgbLedWrite(PIN_WS2812_DIN, 0, 0, 0);
        Serial.println("[LED] CC 状态灯初始化: 模式 4 (WS2812 单线灯 DIN=38, GPIO39已释放给I2S)");
    } else {
        // 分立 3 色 LED 模式
        pinMode(CC_LED_RED_PIN, OUTPUT);
        pinMode(CC_LED_YLW_PIN, OUTPUT);
        pinMode(CC_LED_GRN_PIN, OUTPUT);
        _led_set(LED_OFF, LED_OFF, LED_OFF);
        Serial.println("[LED] CC 状态灯初始化: 分立 3 色 LED (R=38 Y=39 G=40)");
    }
}

void cc_led_update() {
    unsigned long now = millis();
    const char* st = ccStatus.state;

    // ── 状态跳变事件驱动音频通知 ──────────────────────────────
    if (strcmp(st, _last_state) != 0) {
        if (strcmp(st, "success") == 0 || strcmp(st, "idle") == 0) {
            // 从工作中/思考中切换到完成/空闲时播放完成提示音
            if (strcmp(_last_state, "busy") == 0 || strcmp(_last_state, "thinking") == 0) {
                audio_driver_notify(NOTIFY_SUCCESS);
            }
        } else if (strcmp(st, "alarm") == 0 || strcmp(st, "error") == 0) {
            // 异常/告警提示音
            audio_driver_notify(NOTIFY_ALARM);
        } else if (strcmp(st, "busy") == 0 || strcmp(st, "thinking") == 0) {
            // 任务开始/工作中提示音
            if (strcmp(_last_state, "busy") != 0 && strcmp(_last_state, "thinking") != 0) {
                audio_driver_notify(NOTIFY_BUSY);
            }
        }
        strlcpy(_last_state, st, sizeof(_last_state));
    }

    // ── 灯光渲染驱动 ──────────────────────────────────────────
    if (strcmp(st, "alarm") == 0) {
        // 红灯快闪（250ms 周期）
        if (now - _ledTick >= 250) { _ledTick = now; _ledOn = !_ledOn; }
        _led_set(_ledOn ? LED_FULL : LED_OFF, LED_OFF, LED_OFF);
    } else if (strcmp(st, "error") == 0) {
        // 红灯常亮
        _led_set(LED_FULL, LED_OFF, LED_OFF);
    } else if (strcmp(st, "busy") == 0) {
        // 黄灯常亮
        _led_set(LED_OFF, LED_FULL, LED_OFF);
    } else if (strcmp(st, "thinking") == 0) {
        // 黄灯慢闪（1s 周期）
        if (now - _ledTick >= 1000) { _ledTick = now; _ledOn = !_ledOn; }
        _led_set(LED_OFF, _ledOn ? LED_FULL : LED_OFF, LED_OFF);
    } else if (strcmp(st, "success") == 0) {
        // 绿灯常亮（任务刚完成）
        _led_set(LED_OFF, LED_OFF, LED_FULL);
    } else if (strcmp(st, "idle") == 0) {
        // 绿灯常亮（CC 空闲等待下一条指令）
        _led_set(LED_OFF, LED_OFF, LED_FULL);
    } else {
        // off / unknown → 全灭
        _led_set(LED_OFF, LED_OFF, LED_OFF);
    }
}
