/**
 * lv_port_indev.cpp — LVGL v9 输入设备驱动
 * 硬件: TFT_Touch（电阻触摸）+ 四按键 K1-K4
 *
 * 替代原有:
 *   touch.ino   — 触摸扫描 + 手势识别
 *   buttons.ino — 按键扫描 + 消抖 + 短按/长按判定
 *
 * LVGL 内置消抖和长按检测，无需手写状态机。
 */

#include "lv_port_indev.h"
#include <lvgl.h>
#include <TFT_Touch.h>
#include "../config.h"

/* 外部触摸对象（在 PCDash.ino 中定义） */
extern TFT_Touch touch;

/* =================== 原始滑动检测 =================== */

static bool    _tp_was_pressed  = false;
static int16_t _swipe_start_x   = -1;
static int16_t _swipe_last_x    = -1;
static int16_t _swipe_start_y   = -1;
static int16_t _swipe_last_y    = -1;
static uint32_t _swipe_start_ms = 0;
static int8_t  _pending_swipe   = 0;   // -1=左滑, 0=无, 1=右滑
static bool    _swipe_done      = false; // 当次按下已触发过，防止重复

/* 供外部轮询（读后自动清零） */
int8_t lv_port_indev_consume_swipe(void) {
    int8_t dir = _pending_swipe;
    _pending_swipe = 0;
    return dir;
}

/* =================== 触摸输入 =================== */

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (touch.Pressed()) {
        // 翻转 X/Y：触摸屏物理方向与显示旋转方向相差 180°
        int16_t x = (SCREEN_W  - 1) - touch.X();
        int16_t y = (SCREEN_H - 1) - touch.Y();
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        if (!_tp_was_pressed) {
            _swipe_start_x  = x;
            _swipe_start_y  = y;
            _swipe_start_ms = millis();
            _swipe_done     = false;
            _tp_was_pressed = true;
            Serial.printf("[TP] 按下 x=%d y=%d\n", x, y);
        }
        _swipe_last_x = x;
        _swipe_last_y = y;

        // 实时检测：按压过程中位移越阈立即触发，无需等待抬手
        if (!_swipe_done && _swipe_start_x >= 0) {
            int16_t dx = x - _swipe_start_x;
            int16_t dy = y - _swipe_start_y;
            if (abs(dx) >= SWIPE_MIN_DIST && abs(dx) > abs(dy)) {
                _pending_swipe = (dx < 0) ? -1 : 1;
                _swipe_done    = true;
                Serial.printf("[TP] 滑动 dx=%d dy=%d dir=%s\n",
                              dx, dy, dx<0?"LEFT":"RIGHT");
            }
        }
    } else {
        if (_tp_was_pressed) {
            Serial.println("[TP] 释放");
            // 兜底：慢速滑动未在按压中触发时，抬手再判一次
            if (!_swipe_done && _swipe_start_x >= 0) {
                int16_t  dx = _swipe_last_x - _swipe_start_x;
                int16_t  dy = _swipe_last_y - _swipe_start_y;
                uint32_t dt = millis() - _swipe_start_ms;
                if (dt < SWIPE_MAX_MS && abs(dx) >= SWIPE_MIN_DIST && abs(dx) > abs(dy)) {
                    _pending_swipe = (dx < 0) ? -1 : 1;
                    Serial.printf("[TP] 滑动(释放) dx=%d dy=%d dt=%dms dir=%s\n",
                                  dx, dy, dt, dx<0?"LEFT":"RIGHT");
                }
            }
            _swipe_start_x  = -1;
            _swipe_done     = false;
            _tp_was_pressed = false;
        }
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* =================== 初始化入口 =================== */

void lv_port_indev_init(void)
{
    /* 触摸屏校准（原 touch.ino 中的参数） */
    touch.setCal(3583, 678, 3328, 616,
                 SCREEN_W, SCREEN_H, SCREEN_ROTATION);

    /* 注册触摸输入设备 */
    lv_indev_t *tp = lv_indev_create();
    lv_indev_set_type(tp, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(tp, touchpad_read_cb);

    Serial.println("[INDEV] 触摸驱动已初始化");
}
