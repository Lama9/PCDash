#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

void  lv_port_indev_init(void);
int8_t lv_port_indev_consume_swipe(void);  /* -1=左滑, 0=无, 1=右滑 */

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
