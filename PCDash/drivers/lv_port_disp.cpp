/**
 * lv_port_disp.cpp — LVGL v9 显示驱动适配层
 * 硬件: ESP32-S3 + TFT_eSPI + ILI9341 320×240
 *
 * 绘制缓冲策略:
 *   有 PSRAM → 双缓冲全屏（320×240×2 = 150KB，推 DMA 无撕裂）
 *   无 PSRAM → 单缓冲 10 行（320×10×2 = 6.4KB，节省 SRAM）
 */

#include "lv_port_disp.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "../config.h"

/* 外部 TFT 对象（在 PCDash.ino 中定义） */
extern TFT_eSPI tft;

/* =================== 绘制缓冲区 =================== */
#if HAS_PSRAM
  /* 双缓冲：分配在 PSRAM，LVGL 可以在后台刷新前缓冲时渲染下一帧 */
  static const uint32_t BUF_PIXELS = SCREEN_W * SCREEN_H;
  static lv_color_t *draw_buf1 = nullptr;
  static lv_color_t *draw_buf2 = nullptr;
#else
  /* 单缓冲：10 行，约 6.4KB SRAM */
  static const uint32_t BUF_PIXELS = SCREEN_W * 10;
  static lv_color_t draw_buf1[BUF_PIXELS];
#endif

static lv_display_t *disp = nullptr;

/* =================== flush 回调 =================== */
/* LVGL 渲染完成后调用此函数，将像素推送到屏幕 */
static void disp_flush_cb(lv_display_t *display,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    /* px_map 是 lv_color_t 数组，RGB565 格式，直接推送 */
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();

    /* 通知 LVGL 当前 flush 已完成，可继续渲染下一帧 */
    lv_display_flush_ready(display);
}

/* =================== 初始化入口 =================== */
void lv_port_disp_init(void)
{
    /* 背光常亮（GPIO0 是 strapping pin，硬件无调光电路，仅做开关） */
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    /* TFT 初始化 */
    tft.begin();
    tft.setRotation(SCREEN_ROTATION);
#if INVERT_DISPLAY
    tft.invertDisplay(true);
#endif
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);

    /* 分配缓冲区 */
#if HAS_PSRAM
    draw_buf1 = (lv_color_t *)ps_malloc(BUF_PIXELS * sizeof(lv_color_t));
    draw_buf2 = (lv_color_t *)ps_malloc(BUF_PIXELS * sizeof(lv_color_t));
    if (!draw_buf1 || !draw_buf2) {
        Serial.println("[DISP] PSRAM 缓冲分配失败，回退单缓冲");
        if (!draw_buf1) draw_buf1 = (lv_color_t *)malloc(SCREEN_W * 10 * sizeof(lv_color_t));
        draw_buf2 = nullptr;
    }
#endif

    /* 创建 LVGL 显示对象 */
    disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, disp_flush_cb);

#if HAS_PSRAM
    if (draw_buf2) {
        /* 双缓冲全屏模式 */
        lv_display_set_buffers(disp, draw_buf1, draw_buf2,
                               BUF_PIXELS * sizeof(lv_color_t),
                               LV_DISPLAY_RENDER_MODE_FULL);
        Serial.println("[DISP] 双缓冲全屏模式（PSRAM）");
    } else {
        lv_display_set_buffers(disp, draw_buf1, nullptr,
                               SCREEN_W * 10 * sizeof(lv_color_t),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        Serial.println("[DISP] 单缓冲局部模式（PSRAM 不足）");
    }
#else
    lv_display_set_buffers(disp, draw_buf1, nullptr,
                           BUF_PIXELS * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.println("[DISP] 单缓冲局部模式（SRAM）");
#endif

    Serial.printf("[DISP] 初始化完成: %dx%d\n", SCREEN_W, SCREEN_H);
}
