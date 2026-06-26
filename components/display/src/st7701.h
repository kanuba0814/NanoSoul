// ST7701S MIPI-DSI panel bring-up (private to the display component).
// 点屏序列与上电次序原样取自 NanoSoul-Alpha 的 bsp_display_st7701.c
// （WLK2802MIPI-15P V2, ST7701S 2.8" 480x640 模块）。屏不占任何 GPIO。
#pragma once

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_types.h"

// 面板分辨率（lvgl_port 也用）。
#define DISP_H_RES 480
#define DISP_V_RES 640

// 上电 DPHY LDO → DSI bus → DBI → ST7701 厂商序列 → 取帧缓存（PSRAM）。
esp_err_t st7701_init(void);

// 已初始化的面板句柄（lvgl flush 用）。
esp_lcd_panel_handle_t st7701_get_panel(void);

// 注册 DPI "一帧搬完" 回调（lvgl 用来 flush_ready）。
esp_err_t st7701_register_color_trans_done_callback(
    esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx);
