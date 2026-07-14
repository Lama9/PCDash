/**
 * cc_screen.cpp — CC 状态详情屏幕（纯 LVGL，不依赖 Studio）
 *
 * 方案 B：骨架与数据分离
 *   cc_screen_create()  — 建一次骨架（标题栏、列表容器、底部摘要）
 *   cc_screen_update()  — 热刷新数据（颜色/文字），session 数量变化时重建列表行
 *   cc_screen_destroy() — 显式销毁（可选，back_btn_cb 使用 auto_del）
 *
 * 由 app_binddata_update() 每 200ms 调 cc_screen_update()，
 * 若 _scr == NULL（未在CC页面）立即返回。
 */

#include <lvgl.h>
#include "ui/ui.h"
#include "data_model.h"
#include "config.h"

extern CCStatus ccStatus;

// ── 布局常量
#define TITLE_H   16
#define SEP_H      1
#define BOT_H     28    // 底部摘要栏高度（给 montserrat_24 留呼吸空间）
#define LIST_Y    (TITLE_H + SEP_H)
#define LIST_H    (SCREEN_H - LIST_Y - BOT_H)
#define BOT_Y     (SCREEN_H - BOT_H)
#define CARD_H    52    // 每张卡片：14pt ID(上行) + 24pt state(下行) + padding
#define CARD_W    (SCREEN_W - 12)

// ── 每行 session 控件引用
static struct {
    lv_obj_t* row;
    lv_obj_t* dot;
    lv_obj_t* lbl_id;
    lv_obj_t* lbl_state;
} _rows[MAX_CC_SESSIONS];

// ── 屏幕级控件引用
static lv_obj_t* _scr         = NULL;
static lv_obj_t* _list        = NULL;
static lv_obj_t* _lbl_summary = NULL;
static int       _built_count = -1;

// ── 颜色变更检测缓存（避免无谓的 set_style 调用）
static char _last_row_state[MAX_CC_SESSIONS][12];
static char _last_summary_state[12];

// ── 状态 → 颜色
static lv_color_t _state_color(const char* st) {
    if (strcmp(st, "alarm")    == 0) return lv_color_make(220, 50,  50);
    if (strcmp(st, "error")    == 0) return lv_color_make(200, 40,  40);
    if (strcmp(st, "busy")     == 0) return lv_color_make(220, 180,  0);
    if (strcmp(st, "thinking") == 0) return lv_color_make(180, 140,  0);
    if (strcmp(st, "success")  == 0) return lv_color_make(50,  200, 80);
    if (strcmp(st, "idle")     == 0) return lv_color_make(30,  150, 60);
    return lv_color_make(80, 80, 80);
}

// ── 屏幕删除时清空所有指针
static void scr_del_cb(lv_event_t* e) {
    _scr = NULL;
    _list = NULL;
    _lbl_summary = NULL;
    _built_count = -1;
    memset(_rows, 0, sizeof(_rows));
    memset(_last_row_state, 0, sizeof(_last_row_state));
    memset(_last_summary_state, 0, sizeof(_last_summary_state));
}

static void back_btn_cb(lv_event_t* e) {
    lv_screen_load_anim(ui_scrmain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void back_gesture_cb(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        lv_screen_load_anim(ui_scrmain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
    }
}

// ── 重建列表行（session 数量变化时调用）
static void _rebuild_list(int cnt) {
    lv_obj_clean(_list);
    memset(_rows, 0, sizeof(_rows));
    memset(_last_row_state, 0, sizeof(_last_row_state));

    if (cnt == 0) {
        lv_obj_t* hint = lv_label_create(_list);
        lv_label_set_text(hint, "No active CC sessions");
        lv_obj_set_style_text_color(hint, lv_color_make(120, 120, 120),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(hint, CARD_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        _built_count = 0;
        return;
    }

    int n = (cnt < MAX_CC_SESSIONS) ? cnt : MAX_CC_SESSIONS;
    for (int i = 0; i < n; i++) {
        // 卡片
        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, CARD_W, CARD_H);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x001040),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(row, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

        // 状态色圆点
        lv_obj_t* dot = lv_obj_create(row);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(dot, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        // session ID（上行，小字）
        lv_obj_t* lbl_id = lv_label_create(row);
        lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_14,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl_id, lv_color_make(160, 160, 160),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(lbl_id, LV_ALIGN_TOP_LEFT, 18, 0);

        // 状态文字（下行，大字，带状态色）
        lv_obj_t* lbl_state = lv_label_create(row);
        lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_14,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(lbl_state, LV_ALIGN_BOTTOM_LEFT, 18, 0);

        _rows[i].row       = row;
        _rows[i].dot       = dot;
        _rows[i].lbl_id    = lbl_id;
        _rows[i].lbl_state = lbl_state;
    }

    _built_count = cnt;
}

// ── 热刷新（每 200ms 由 app_binddata_update 调用）
void cc_screen_update(void) {
    if (!_scr) return;

    int cnt = ccStatus.session_count;

    if (cnt != _built_count) {
        _rebuild_list(cnt);
    }

    // 更新各行文字与颜色
    int n = (cnt < MAX_CC_SESSIONS) ? cnt : MAX_CC_SESSIONS;
    for (int i = 0; i < n; i++) {
        if (!_rows[i].row) continue;
        const char* st  = ccStatus.sessions[i].state;
        const char* sid = ccStatus.sessions[i].id;

        // 文字：LVGL v9 内部有 memcmp，相同则跳过重绘
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "#%d  %.8s", i + 1, sid);
        lv_label_set_text(_rows[i].lbl_id, id_buf);
        lv_label_set_text(_rows[i].lbl_state, st);

        // 颜色：只在 state 变化时才调用 set_style（避免每 200ms 标脏）
        if (strcmp(_last_row_state[i], st) != 0) {
            strlcpy(_last_row_state[i], st, sizeof(_last_row_state[i]));
            lv_color_t clr = _state_color(st);
            lv_obj_set_style_bg_color(_rows[i].dot, clr, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(_rows[i].lbl_state, clr, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // 更新底部摘要
    if (_lbl_summary) {
        char sum[48];
        snprintf(sum, sizeof(sum), "%d session(s)  top: %s",
                 ccStatus.count, ccStatus.state);
        lv_label_set_text(_lbl_summary, sum);

        // 颜色同样只在状态变化时更新
        if (strcmp(_last_summary_state, ccStatus.state) != 0) {
            strlcpy(_last_summary_state, ccStatus.state, sizeof(_last_summary_state));
            lv_obj_set_style_text_color(_lbl_summary, _state_color(ccStatus.state),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

lv_obj_t* cc_screen_create(void) {
    if (_scr) {
        lv_obj_delete(_scr);
        _scr = NULL;
    }

    // ── 根屏幕
    _scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(_scr, scr_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(_scr, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(0x00164A),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_scr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_scr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_scr, back_gesture_cb, LV_EVENT_GESTURE, NULL);

    // ── 标题栏
    lv_obj_t* titlebar = lv_obj_create(_scr);
    lv_obj_set_size(titlebar, SCREEN_W, TITLE_H);
    lv_obj_set_pos(titlebar, 0, 0);
    lv_obj_remove_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(titlebar, lv_color_hex(0x00164A),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(titlebar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(titlebar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(titlebar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(titlebar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* lbl_title = lv_label_create(titlebar);
    lv_label_set_text(lbl_title, "Claude Code Status");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* btn_back = lv_btn_create(titlebar);
    lv_obj_set_size(btn_back, 42, 14);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x003080),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_set_style_text_color(lbl_back, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_12,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_back);

    // ── 分隔线
    lv_obj_t* div = lv_obj_create(_scr);
    lv_obj_set_size(div, SCREEN_W, SEP_H);
    lv_obj_set_pos(div, 0, TITLE_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x1E4080),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(div, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(div, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── session 列表容器（固定高度，垂直滚动）
    _list = lv_obj_create(_scr);
    lv_obj_set_size(_list, SCREEN_W, LIST_H);
    lv_obj_set_pos(_list, 0, LIST_Y);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x00164A),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(_list, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(_list, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    // 保留默认 SCROLLABLE

    // ── 底部摘要栏（固定，不随列表滚动）
    lv_obj_t* bot = lv_obj_create(_scr);
    lv_obj_set_size(bot, SCREEN_W, BOT_H);
    lv_obj_set_pos(bot, 0, BOT_Y);
    lv_obj_remove_flag(bot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0x001040),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bot, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    _lbl_summary = lv_label_create(bot);
    lv_obj_set_style_text_font(_lbl_summary, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(_lbl_summary, LV_ALIGN_CENTER, 0, 0);

    // ── 首次填充数据
    _built_count = -1;
    cc_screen_update();

    return _scr;
}
