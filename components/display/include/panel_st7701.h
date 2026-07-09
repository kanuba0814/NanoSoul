#pragma once
/*
 * Public handle to the ST7701 MIPI-DSI panel bring-up.
 *
 * display_init() (in this component) wires the panel to LVGL for the legacy
 * TESTPANEL mode. FACE mode instead drives the panel directly with the emote
 * gfx runtime, so it needs the raw panel without LVGL — that's what these three
 * functions give it. The panel is 480x640 RGB565, double-buffered, portrait.
 */

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_H_RES 480
#define PANEL_V_RES 640

// Power the DPHY LDO, bring up DSI/DBI/ST7701, allocate frame buffers.
esp_err_t st7701_init(void);

// Panel handle for esp_lcd_panel_draw_bitmap(), or NULL before init.
esp_lcd_panel_handle_t st7701_get_panel(void);

// Register the DPI "one frame flushed" callback (drives flush-ready handshakes).
esp_err_t st7701_register_color_trans_done_callback(
    esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
