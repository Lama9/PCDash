// cc_led.ino — CC 状态 LED 控制（红/黄/绿，共阴 PWM 驱动）

#include "config.h"
#include "data_model.h"

extern CCStatus ccStatus;

#define LED_FULL  180   // PWM 满亮（0-255），非255防止直视刺眼
#define LED_OFF     0

static unsigned long _ledTick = 0;
static bool          _ledOn   = false;

static void _led_set(int r, int y, int g) {
    analogWrite(CC_LED_RED_PIN, r);
    analogWrite(CC_LED_YLW_PIN, y);
    analogWrite(CC_LED_GRN_PIN, g);
}

void cc_led_setup() {
    pinMode(CC_LED_RED_PIN, OUTPUT);
    pinMode(CC_LED_YLW_PIN, OUTPUT);
    pinMode(CC_LED_GRN_PIN, OUTPUT);
    _led_set(LED_OFF, LED_OFF, LED_OFF);
    Serial.println("[LED] CC 状态灯初始化完成 R=38 Y=39 G=40");
}

void cc_led_update() {
    unsigned long now = millis();
    const char* st = ccStatus.state;

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
