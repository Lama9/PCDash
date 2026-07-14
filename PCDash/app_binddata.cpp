/**
 * app_binddata.cpp — 数据绑定层
 * 将 MonitorData / AppState 映射到 SquareLine 导出的 LVGL 控件
 *
 * 依赖:
 *   - ui/ui.h         (SquareLine 导出)
 *   - data_model.h    (MonitorData / AppState)
 *   - TimeLib         (hour/minute/day/month/year)
 */

#include "app_binddata.h"
#include <lvgl.h>
#include <TimeLib.h>
#include "ui/ui.h"
#include "data_model.h"
#include "config.h"

// PCDash.ino 中定义的全局变量
extern MonitorData pcData[];
extern AppState    app;
extern CCStatus    ccStatus;
extern SemaphoreHandle_t g_data_mutex;

// data_hub.ino 中定义
extern MonitorData* datahub_currentData();

// cc_screen.cpp 中定义
extern void cc_screen_update(void);

// ── 进度条上限（用于 W → % 换算）───────────────────────────
#define MAX_CPU_PWR  200    // W
#define MAX_GPU_PWR  350    // W

// ── 辅助：整数安全截断 ──────────────────────────────────────
static inline int clamp100(int v) {
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

// ── 辅助：网速字符串（带方向前缀）──────────────────────────
static void fmt_speed(char prefix, long bps, char* buf, size_t len) {
    if (bps < 0) bps = 0;
    char num[16];
    if      (bps < 1024LL)             snprintf(num, sizeof(num), "%ldB/s",  bps);
    else if (bps < 1024LL * 1024)      snprintf(num, sizeof(num), "%ldK/s",  bps / 1024);
    else if (bps < 1024LL * 1024 * 1024) snprintf(num, sizeof(num), "%.1fM/s", bps / 1048576.0f);
    else                               snprintf(num, sizeof(num), "%.1fG/s", bps / 1073741824.0f);
    snprintf(buf, len, "%c%s", prefix, num);
}

// ── 辅助：Bar 前景色（绿/黄/红）────────────────────────────
static void set_bar_color(lv_obj_t* bar, int val, int warn, int crit) {
    lv_color_t clr;
    if      (val > crit) clr = lv_color_make(220, 50,  50);  // 红：超限
    else if (val > warn) clr = lv_color_make(220, 180,  0);  // 黄：警告
    else                 clr = lv_color_make(50,  200, 80);   // 绿：正常
    lv_obj_set_style_bg_color(bar, clr, LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

// ── 辅助：点指示器颜色 ──────────────────────────────────────
static void set_dot_color(lv_obj_t* dot, bool active) {
    lv_color_t clr = active ? lv_color_white() : lv_color_make(66, 66, 66);
    lv_obj_set_style_bg_color(dot, clr, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ════════════════════════════════════════════════════════════
// 各区域绑定函数
// ════════════════════════════════════════════════════════════

static void bind_statusbar(const MonitorData* d) {
    char buf[28];

    fmt_speed('^', d->net.up, buf, sizeof(buf));
    lv_label_set_text(ui_lblnetup, buf);

    fmt_speed('v', d->net.dn, buf, sizeof(buf));
    lv_label_set_text(ui_lblnetdn, buf);

    lv_label_set_text(ui_lbldevicename, d->id[0] ? d->id : "---");

    const char* conn;
    if (!d->connected)       conn = "---";
    else if (d->source == 1) conn = "WiFi";
    else if (d->source == 2) conn = "Serial";
    else                     conn = "---";
    lv_label_set_text(ui_lblconntype, conn);
}

static void bind_cpu(const MonitorData* d) {
    char buf[16];

    // 标题：CPU 型号
    lv_label_set_text(ui_lblcputitle, d->cpuName[0] ? d->cpuName : "CPU");

    // USG
    lv_bar_set_value(ui_barcpuusg, clamp100((int)d->cpu), LV_ANIM_ON);
    set_bar_color(ui_barcpuusg, (int)d->cpu, 60, 90);
    snprintf(buf, sizeof(buf), "%d%%", (int)d->cpu);
    lv_label_set_text(ui_lblcpuusgval, buf);

    // TMP (°C 直接映射 0-100，颜色按实际温度判断)
    if (d->cpuTemp < 0) {
        lv_bar_set_value(ui_barcputmp, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_lblcputmpval, "--");
    } else {
        lv_bar_set_value(ui_barcputmp, clamp100(d->cpuTemp), LV_ANIM_ON);
        set_bar_color(ui_barcputmp, d->cpuTemp, 70, 90);
        snprintf(buf, sizeof(buf), "%d\xc2\xb0""C", d->cpuTemp);
        lv_label_set_text(ui_lblcputmpval, buf);
    }

    // PWR (W → %)
    if (d->cpuPower <= 0) {
        lv_bar_set_value(ui_barcpupwr, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_lblcpupwrval, "--");
    } else {
        int pct = clamp100(d->cpuPower * 100 / MAX_CPU_PWR);
        lv_bar_set_value(ui_barcpupwr, pct, LV_ANIM_ON);
        set_bar_color(ui_barcpupwr, pct, 60, 90);
        snprintf(buf, sizeof(buf), "%dW", d->cpuPower);
        lv_label_set_text(ui_lblcpupwrval, buf);
    }

    // MEM
    lv_bar_set_value(ui_barcpumem, clamp100((int)d->mem.pct), LV_ANIM_ON);
    set_bar_color(ui_barcpumem, (int)d->mem.pct, 60, 90);
    snprintf(buf, sizeof(buf), "%.1fG", d->mem.used);
    lv_label_set_text(ui_lblcpumemval, buf);
}

static void bind_gpu(const MonitorData* d) {
    if (!d->gpuValid) {
        lv_label_set_text(ui_lblgputitle, "GPU");
        lv_bar_set_value(ui_bargpuusg, 0, LV_ANIM_OFF);
        lv_bar_set_value(ui_bargputmp, 0, LV_ANIM_OFF);
        lv_bar_set_value(ui_bargpupwr, 0, LV_ANIM_OFF);
        lv_bar_set_value(ui_bargpumem, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_lblgpuusgval, "--");
        lv_label_set_text(ui_lblgputmpval, "--");
        lv_label_set_text(ui_lblgpupwrval, "--");
        lv_label_set_text(ui_lblgpumemval, "--");
        return;
    }

    char buf[16];
    const GpuData* g = &d->gpu;

    // 标题：GPU 型号
    lv_label_set_text(ui_lblgputitle, g->name[0] ? g->name : "GPU");

    // USG
    lv_bar_set_value(ui_bargpuusg, clamp100(g->usage), LV_ANIM_ON);
    set_bar_color(ui_bargpuusg, g->usage, 60, 90);
    snprintf(buf, sizeof(buf), "%d%%", g->usage);
    lv_label_set_text(ui_lblgpuusgval, buf);

    // TMP（颜色按实际温度判断，GPU 阈值略低于 CPU）
    lv_bar_set_value(ui_bargputmp, clamp100(g->temp), LV_ANIM_ON);
    set_bar_color(ui_bargputmp, g->temp, 70, 85);
    snprintf(buf, sizeof(buf), "%d\xc2\xb0""C", g->temp);
    lv_label_set_text(ui_lblgputmpval, buf);

    // PWR
    if (g->gpuPower <= 0) {
        lv_bar_set_value(ui_bargpupwr, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_lblgpupwrval, "--");
    } else {
        int pct = clamp100(g->gpuPower * 100 / MAX_GPU_PWR);
        lv_bar_set_value(ui_bargpupwr, pct, LV_ANIM_ON);
        set_bar_color(ui_bargpupwr, pct, 60, 90);
        snprintf(buf, sizeof(buf), "%dW", g->gpuPower);
        lv_label_set_text(ui_lblgpupwrval, buf);
    }

    // MEM
    lv_bar_set_value(ui_bargpumem, clamp100(g->mem_pct), LV_ANIM_ON);
    set_bar_color(ui_bargpumem, g->mem_pct, 60, 90);
    snprintf(buf, sizeof(buf), "%d%%", g->mem_pct);
    lv_label_set_text(ui_lblgpumemval, buf);
}

static void bind_lower(const MonitorData* d) {
    // 日期（优先使用 MonitorData，NTP 作为回退）
    if (d->dateStr[0]) {
        lv_label_set_text(ui_lbldate, d->dateStr);
    } else if (timeStatus() != timeNotSet) {
        char dbuf[12];
        snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d", year(), month(), day());
        lv_label_set_text(ui_lbldate, dbuf);
    } else {
        lv_label_set_text(ui_lbldate, "--/--");
    }

    // 天气（城市 / 描述 / 温度 / 湿度 / 风力风向）
    if (d->wthValid) {
        char tbuf[8];
        lv_label_set_text(ui_lblcity,    d->wth.city[0] ? d->wth.city : "");
        lv_label_set_text(ui_lblweather, d->wth.desc[0] ? d->wth.desc : "");
        snprintf(tbuf, sizeof(tbuf), "%d\xc2\xb0""C", d->wth.temp);
        lv_label_set_text(ui_lblwthtemp, tbuf);
        snprintf(tbuf, sizeof(tbuf), "%d%%", d->wth.humidity);
        lv_label_set_text(ui_lblwthhumidity, tbuf);
        lv_label_set_text(ui_lblwind, d->wth.wind[0] ? d->wth.wind : "--");
    } else {
        lv_label_set_text(ui_lblcity,        "--");
        lv_label_set_text(ui_lblweather,     "---");
        lv_label_set_text(ui_lblwthtemp,     "--");
        lv_label_set_text(ui_lblwthhumidity, "--");
        lv_label_set_text(ui_lblwind,        "--");
    }

    // 室内温湿度（DHT11 本地传感器）
    if (app.roomValid) {
        char rbuf[8];
        snprintf(rbuf, sizeof(rbuf), "%d\xc2\xb0""C", app.roomTemp);
        lv_label_set_text(ui_lblroomtemp, rbuf);
        snprintf(rbuf, sizeof(rbuf), "%d%%", app.roomHumidity);
        lv_label_set_text(ui_lblroomhumidity, rbuf);
    } else {
        lv_label_set_text(ui_lblroomtemp,     "--");
        lv_label_set_text(ui_lblroomhumidity, "--");
    }

    // 农历
    if (d->lunValid) {
        char lbuf[32];
        snprintf(lbuf, sizeof(lbuf), "%s%s", d->lun.mo, d->lun.dy);
        lv_label_set_text(ui_lbllunarmonthday, lbuf);
        lv_label_set_text(ui_lbllunarterm,
            d->lun.jq[0] ? d->lun.jq : " ");
        lv_label_set_text(ui_lbllunartext, d->lun.gz_y);
    } else {
        lv_label_set_text(ui_lbllunarmonthday, "--");
        lv_label_set_text(ui_lbllunarterm, " ");
        lv_label_set_text(ui_lbllunartext, " ");
    }
}

static void bind_cc_status(void) {
    const char* st = ccStatus.state;

    // 按钮背景色与 LED 颜色对应
    lv_color_t clr;
    if      (strcmp(st, "alarm")    == 0) clr = lv_color_make(220, 50,  50);
    else if (strcmp(st, "error")    == 0) clr = lv_color_make(200, 40,  40);
    else if (strcmp(st, "busy")     == 0) clr = lv_color_make(220, 180,  0);
    else if (strcmp(st, "thinking") == 0) clr = lv_color_make(180, 140,  0);
    else if (strcmp(st, "success")  == 0) clr = lv_color_make(50,  200, 80);
    else if (strcmp(st, "idle")     == 0) clr = lv_color_make(30,  150, 60);
    else                                   clr = lv_color_make(60,  60,  60);
    lv_obj_set_style_bg_color(ui_ButCCStatus, clr, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 标签：[CC-N]
    char buf[16];
    snprintf(buf, sizeof(buf), "[CC-%d]", ccStatus.count);
    lv_label_set_text(ui_lblCCStatus, buf);
}

static void bind_dots(void) {
    for (int i = 0; i < MAX_PC_COUNT; i++) {
        lv_obj_t* dot = lv_obj_get_child(ui_pnldots, i);
        if (!dot) continue;
        if (i < (int)app.pcCount) {
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            set_dot_color(dot, (i == (int)app.currentPC));
        } else {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ════════════════════════════════════════════════════════════
// 公开接口
// ════════════════════════════════════════════════════════════

void app_binddata_init(void) {
    // 写入不随数据变化的静态标签文字
    lv_label_set_text(ui_lblCCStatus,  "[CC-0]");
    lv_obj_set_style_bg_color(ui_ButCCStatus, lv_color_make(60, 60, 60),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_lblcputitle,     "---");   // 会在首次数据更新时替换为型号
    lv_label_set_text(ui_lblgputitle,     "---");
    lv_label_set_text(ui_lblcity,         "--");
    lv_label_set_text(ui_lblweather,      "---");
    lv_label_set_text(ui_lblwthtemp,      "--");
    lv_label_set_text(ui_lblwthhumidity,  "--");
    lv_label_set_text(ui_lblwind,         "--");
    lv_label_set_text(ui_lblroomtemp,     "--");
    lv_label_set_text(ui_lblroomhumidity, "--");
    lv_label_set_text(ui_lblcpuusgtag, "USG");
    lv_label_set_text(ui_lblcputmptag, "TMP");
    lv_label_set_text(ui_lblcpupwrtag, "PWR");
    lv_label_set_text(ui_lblcpumemtag, "MEM");
    lv_label_set_text(ui_lblgpuusgtag, "USG");
    lv_label_set_text(ui_lblgputmptag, "TMP");
    lv_label_set_text(ui_lblgpupwrtag, "PWR");
    lv_label_set_text(ui_lblgpumemtag, "MEM");

    // 进度条初始值
    lv_bar_set_value(ui_barcpuusg, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_barcputmp, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_barcpupwr, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_barcpumem, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_bargpuusg, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_bargputmp, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_bargpupwr, 0, LV_ANIM_OFF);
    lv_bar_set_value(ui_bargpumem, 0, LV_ANIM_OFF);
}

void app_binddata_tick_clock(void);   // 前置声明

void app_binddata_update(void) {
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);

    MonitorData* d = datahub_currentData();
    if (!d) { xSemaphoreGive(g_data_mutex); return; }

    bind_statusbar(d);
    bind_cc_status();
    bind_cpu(d);
    bind_gpu(d);
    bind_lower(d);
    bind_dots();
    cc_screen_update();   // 若在CC详情页则热刷新，否则立即返回

    xSemaphoreGive(g_data_mutex);

    // 时钟优先读 NTP（无共享数据竞争），不需要锁
    app_binddata_tick_clock();
}

void app_binddata_tick_clock(void) {
    // 优先 NTP
    if (timeStatus() != timeNotSet) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", hour(), minute());
        lv_label_set_text(ui_lbltime, tbuf);
        return;
    }
    // NTP 未同步 → 降级为串口推送的 PC 时间（格式 "HH:MM:SS"，只取前5位）
    MonitorData* d = datahub_currentData();
    if (d && d->timeStr[0]) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%.5s", d->timeStr);
        lv_label_set_text(ui_lbltime, tbuf);
    } else {
        lv_label_set_text(ui_lbltime, "--:--");
    }
}
