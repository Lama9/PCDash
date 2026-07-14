/**
 * ui_events.cpp — LVGL 事件回调
 *
 * 手势: 左滑 → 下一台PC，右滑 → 上一台PC
 * 按键: K1(LEFT)→上一PC, K2(RIGHT)→下一PC, K3(ENTER)→保留
 * 触摸: ui_ButCCStatus 点击 → 打开 CC 详情屏幕
 */

#include <lvgl.h>
#include "ui/ui.h"
#include "data_model.h"
#include "config.h"

// PCDash.ino 中定义
extern AppState app;

// data_hub.ino 中定义
extern void datahub_prevPC();
extern void datahub_nextPC();

// 包含头文件以确保 extern "C" linkage 一致
#include "drivers/lv_port_disp.h"

// cc_screen.cpp 提供
extern lv_obj_t* cc_screen_create(void);

// ── CC 状态按钮回调 ───────────────────────────────────────────
static void cc_btn_cb(lv_event_t* e) {
    Serial.println("[CC_BTN] 点击事件触发");
    lv_obj_t* scr = cc_screen_create();
    Serial.printf("[CC_BTN] cc_screen_create() = %p\n", (void*)scr);
    if (scr) {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        Serial.println("[CC_BTN] 屏幕切换动画已启动");
    }
}

// ── 触摸调试回调（确认 LVGL 层是否收到触摸）────────────────────
static void touch_debug_cb(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    Serial.printf("[LVGL] PRESSED  x=%d y=%d  obj=%p\n",
                  (int)pt.x, (int)pt.y, (void*)target);
}

// ── 按键事件回调（绑定在 scrmain 上）────────────────────────
static void key_cb(lv_event_t* e) {
    uint32_t key = lv_event_get_key(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_KEY) {
        switch (key) {
            case LV_KEY_LEFT:
                datahub_prevPC();
                app.lastManualSwitch = millis();
                break;
            case LV_KEY_RIGHT:
                datahub_nextPC();
                app.lastManualSwitch = millis();
                break;
            default:
                break;
        }
    }
}

// ── 初始化入口 ────────────────────────────────────────────────
void ui_events_init(void) {
    // 触摸滑动切换设备由 lv_port_indev_consume_swipe() 在 loop() 中处理

    // 按键：将 scrmain 加入输入组，接收键盘设备事件
    lv_group_t* grp = lv_group_create();
    lv_group_add_obj(grp, ui_scrmain);

    lv_indev_t* kb = lv_indev_get_next(NULL);
    while (kb) {
        if (lv_indev_get_type(kb) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(kb, grp);
            break;
        }
        kb = lv_indev_get_next(kb);
    }

    lv_obj_add_event_cb(ui_scrmain, key_cb, LV_EVENT_KEY, NULL);

    // CC 状态按钮点击 → 打开详情屏幕
    lv_obj_add_event_cb(ui_ButCCStatus, cc_btn_cb, LV_EVENT_CLICKED, NULL);

    // 触摸调试：主屏 + CC按钮均监听 PRESSED，确认 LVGL 坐标命中
    lv_obj_add_event_cb(ui_scrmain,    touch_debug_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_ButCCStatus, touch_debug_cb, LV_EVENT_PRESSED, NULL);

    Serial.println("[UI_EVENTS] 手势 + 按键 + CC按钮事件已注册");
}
