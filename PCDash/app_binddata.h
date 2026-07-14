#ifndef APP_BINDDATA_H
#define APP_BINDDATA_H

#ifdef __cplusplus
extern "C" {
#endif

// 首次初始化：写入不变的静态文本（标签标题等）
void app_binddata_init(void);

// 数据全量刷新：将当前 PC 数据同步到所有 LVGL 控件
void app_binddata_update(void);

// 轻量时钟刷新：每秒只更新 ui_lbltime（避免全量刷新）
void app_binddata_tick_clock(void);

#ifdef __cplusplus
}
#endif

#endif // APP_BINDDATA_H
