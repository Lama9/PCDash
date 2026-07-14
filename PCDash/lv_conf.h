/**
 * lv_conf.h — LVGL v9.3 配置
 * 目标平台: ESP32-S3 + ILI9341 320×240
 */

#if 1  /* 设为 1 才启用此文件 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ==================
 * 颜色设置
 * ================== */
#define LV_COLOR_DEPTH 16   /* RGB565，匹配 TFT_eSPI */

/* ==================
 * 内存设置
 * ================== */
/* LVGL 内部堆大小（ESP32-S3 有 PSRAM，可调大；无 PSRAM 时建议 48KB） */
#define LV_MEM_SIZE (64 * 1024U)

/* 使用 LVGL 内置内存分配器 */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

/* ==================
 * HAL 设置
 * ================== */
/* LVGL tick 周期（毫秒），需在 loop() 中每 LV_TICK_PERIOD_MS ms 调用 lv_tick_inc() */
#define LV_TICK_PERIOD_MS 2

/* ==================
 * 绘制配置
 * ================== */
/* 梯度缓冲区大小（单位: 像素），用于渐变效果 */
#define LV_DRAW_BUF_STRIDE_ALIGN   1
#define LV_DRAW_BUF_ALIGN          4

/* 启用软件渲染（不使用 GPU） */
#define LV_USE_DRAW_SW 1

/* ==================
 * 日志设置
 * ================== */
/* 调试阶段开启，正式发布可设为 0 节省 Flash */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN   /* 仅显示警告和错误 */
#define LV_LOG_PRINTF 1                  /* 使用 printf 输出（Serial.print 需另行适配） */

/* ==================
 * 断言
 * ================== */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* 自定义 assert 处理: abort() 触发 ESP32 panic handler
 * → 输出完整调用栈 (backtrace) + 自动重启
 * → 替代默认的 while(1) 静默卡死 */
#include <stdlib.h>
#define LV_ASSERT_HANDLER  abort();

/* ==================
 * 性能监视器（调试用，正式版关闭）
 * ================== */
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

/* ==================
 * 字体配置
 * ================== */
/* 内置 Montserrat 字体（英文/数字） */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* 默认字体（用于未指定字体的控件） */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* 启用自定义字体解码（SquareLine 导出的 .c 字体文件需要此项） */
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1

/* ==================
 * 文字渲染
 * ================== */
/* 双向文字（阿拉伯语等），不需要关闭 */
#define LV_USE_BIDI 0
/* 阿拉伯语字形整形，不需要 */
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* ==================
 * Widget 控件开关
 * ================== */
#define LV_USE_ARC        1
#define LV_USE_BAR        1   /* 进度条，用于 CPU/GPU/内存显示 */
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0   /* 如后续需要折线图可开启 */
#define LV_USE_CHECKBOX   0
#define LV_USE_COLORWHEEL 0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1   /* 图片控件，用于天气图标/背景图 */
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1   /* 文字标签，大量使用 */
#define LV_USE_LED        0
#define LV_USE_LINE       1   /* 分割线 */
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1   /* 加载动画，用于等待数据状态 */
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* ==================
 * 主题
 * ================== */
/* 默认主题（支持暗色/亮色切换） */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1        /* 默认暗色主题 */
#define LV_THEME_DEFAULT_GROW 0

/* 简单主题（备用） */
#define LV_USE_THEME_SIMPLE   1
#define LV_USE_THEME_MONO     0

/* ==================
 * 布局
 * ================== */
#define LV_USE_FLEX  1   /* Flexbox 布局 */
#define LV_USE_GRID  1   /* Grid 布局 */

/* ==================
 * 图片解码器
 * ================== */
/* 内置 BMP 解码 */
#define LV_USE_BMP 0
/* JPEG 解码（ESP32-S3 用 TJpgDec，不走 LVGL 内置） */
#define LV_USE_TJPGD 0
/* PNG 解码 */
#define LV_USE_PNG 0
/* GIF 解码 */
#define LV_USE_GIF 0

/* ==================
 * 文件系统（背景图使用 FFat）
 * ================== */
#define LV_USE_FS_FATFS  0   /* 不用 FatFs，改用 Arduino ESP FFat */
#define LV_USE_FS_POSIX  0
#define LV_USE_FS_WIN32  0
#define LV_USE_FS_STDIO  0

/* ==================
 * 输入设备
 * ================== */
/* 触摸点击判断阈值（像素），防抖用 */
#define LV_INDEV_DEF_READ_PERIOD          10   /* ms */
#define LV_INDEV_DEF_DRAG_LIMIT           10   /* px，滑动识别最小距离 */
#define LV_INDEV_DEF_DRAG_THROW           10
#define LV_INDEV_DEF_LONG_PRESS_TIME      800  /* ms，长按判定 */
#define LV_INDEV_DEF_LONG_PRESS_REP_TIME  300  /* ms，长按重复间隔 */
#define LV_INDEV_DEF_GESTURE_LIMIT        50   /* px，手势最小距离 */
#define LV_INDEV_DEF_GESTURE_MIN_VELOCITY 3

/* ==================
 * 动画
 * ================== */
#define LV_USE_ANIM 1   /* 启用动画（进度条更新动画用） */

/* ==================
 * 其他功能
 * ================== */
#define LV_USE_SNAPSHOT   0
#define LV_USE_MONKEY     0
#define LV_USE_GRIDNAV    0
#define LV_USE_FRAGMENT   0
#define LV_USE_IMGFONT    0
#define LV_USE_OBSERVER   1   /* 数据绑定观察者模式（可选） */
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

#endif  /* LV_CONF_H */
#endif  /* 文件启用标志 */
