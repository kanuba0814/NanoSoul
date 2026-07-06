// 最小显示门面：点亮 ST7701 MIPI-DSI 屏 + 起 LVGL（可选触摸）。
// 给测试固件一个「在屏上写状态文字 + 触摸点按钮」的最短路径。
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 一把梭：ST7701 点屏 → LVGL port → 起 LVGL 任务。成功后默认屏已激活。
esp_err_t display_init(void);

// 把 FT6x36 触摸（board I²C0）接成 LVGL pointer indev。display_init 之后调。
// 探不到触摸时返回其错误但不致命——屏仍可显示，只是没有输入。
esp_err_t display_touch_init(i2c_master_bus_handle_t bus);

// LVGL 非线程安全：在 LVGL 任务之外调任何 lv_* 前后必须 lock/unlock。
void display_lock(void);
void display_unlock(void);

// 当前 lv_display 句柄（一般用不到，调试用）。
lv_display_t *display_get_lv(void);

#ifdef __cplusplus
}
#endif
