// 最小显示门面：点亮 ST7701 MIPI-DSI 屏 + 起 LVGL（无触摸）。
// 给测试固件一个「在屏上写状态文字」的最短路径。
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 一把梭：ST7701 点屏 → LVGL port → 起 LVGL 任务。成功后默认屏已激活。
esp_err_t display_init(void);

// LVGL 非线程安全：在 LVGL 任务之外调任何 lv_* 前后必须 lock/unlock。
void display_lock(void);
void display_unlock(void);

// 当前 lv_display 句柄（一般用不到，调试用）。
lv_display_t *display_get_lv(void);

#ifdef __cplusplus
}
#endif
