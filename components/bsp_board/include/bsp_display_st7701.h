#pragma once

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include <stdint.h>

esp_err_t bsp_display_st7701_init(void);
esp_err_t bsp_display_st7701_fill(uint16_t rgb565);
esp_lcd_panel_handle_t bsp_display_st7701_get_panel(void);
esp_err_t bsp_display_st7701_register_color_trans_done_callback(
    esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx);
