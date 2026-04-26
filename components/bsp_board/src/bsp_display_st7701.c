#include "bsp_display_st7701.h"
#include "bsp_board_pins.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7701.h"

static const char *TAG = "disp";

static esp_ldo_channel_handle_t s_dphy_ldo = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus  = NULL;
static esp_lcd_panel_io_handle_t s_io      = NULL;
static esp_lcd_panel_handle_t s_panel      = NULL;
#define DISPLAY_NUM_FBS 2
static uint16_t *s_fbs[DISPLAY_NUM_FBS]    = { 0 };
static uint8_t s_front_fb_index            = 0;

/*
 * Vendor init sequence translated verbatim from
 *   docs/Disp/初始化代码 ST7701S+28-480640_INIT.txt
 * supplied by Waveshare reseller for the WLK2802MIPI-15P V2 (480x640) module.
 * Tuple layout: { cmd, data_ptr, data_bytes, delay_ms }.
 */
static const st7701_lcd_init_cmd_t vendor_specific_init[] = {
    { 0xFF, (uint8_t[]){ 0x77, 0x01, 0x00, 0x00, 0x13 }, 5, 0 },
    { 0xEF, (uint8_t[]){ 0x08 }, 1, 0 },
    { 0xFF, (uint8_t[]){ 0x77, 0x01, 0x00, 0x00, 0x10 }, 5, 0 },
    { 0xC0, (uint8_t[]){ 0x4F, 0x00 }, 2, 0 },
    { 0xC1, (uint8_t[]){ 0x10, 0x0C }, 2, 0 },
    { 0xC2, (uint8_t[]){ 0x01, 0x14 }, 2, 0 },
    { 0xCC, (uint8_t[]){ 0x10 }, 1, 0 },
    { 0xB0, (uint8_t[]){ 0x0A, 0x18, 0x1E, 0x12, 0x16, 0x0C, 0x0E, 0x0D,
                          0x0C, 0x29, 0x06, 0x14, 0x13, 0x29, 0x33, 0x1C }, 16, 0 },
    { 0xB1, (uint8_t[]){ 0x0A, 0x19, 0x21, 0x0A, 0x0C, 0x00, 0x0C, 0x03,
                          0x03, 0x23, 0x01, 0x0E, 0x0C, 0x27, 0x2B, 0x1C }, 16, 0 },
    { 0xFF, (uint8_t[]){ 0x77, 0x01, 0x00, 0x00, 0x11 }, 5, 0 },
    { 0xB0, (uint8_t[]){ 0x5D }, 1, 0 },
    { 0xB1, (uint8_t[]){ 0x61 }, 1, 0 },
    { 0xB2, (uint8_t[]){ 0x84 }, 1, 0 },
    { 0xB3, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0xB5, (uint8_t[]){ 0x4D }, 1, 0 },
    { 0xB7, (uint8_t[]){ 0x85 }, 1, 0 },
    { 0xB8, (uint8_t[]){ 0x20 }, 1, 0 },
    { 0xC1, (uint8_t[]){ 0x78 }, 1, 0 },
    { 0xC2, (uint8_t[]){ 0x78 }, 1, 0 },
    { 0xD0, (uint8_t[]){ 0x88 }, 1, 0 },
    { 0xE0, (uint8_t[]){ 0x00, 0x00, 0x02 }, 3, 0 },
    { 0xE1, (uint8_t[]){ 0x06, 0xA0, 0x08, 0xA0, 0x05, 0xA0, 0x07, 0xA0,
                          0x00, 0x44, 0x44 }, 11, 0 },
    { 0xE2, (uint8_t[]){ 0x20, 0x20, 0x44, 0x44, 0x96, 0xA0, 0x00, 0x00,
                          0x96, 0xA0, 0x00, 0x00 }, 12, 0 },
    { 0xE3, (uint8_t[]){ 0x00, 0x00, 0x22, 0x22 }, 4, 0 },
    { 0xE4, (uint8_t[]){ 0x44, 0x44 }, 2, 0 },
    { 0xE5, (uint8_t[]){ 0x0D, 0x91, 0xA0, 0xA0, 0x0F, 0x93, 0xA0, 0xA0,
                          0x09, 0x8D, 0xA0, 0xA0, 0x0B, 0x8F, 0xA0, 0xA0 }, 16, 0 },
    { 0xE6, (uint8_t[]){ 0x00, 0x00, 0x22, 0x22 }, 4, 0 },
    { 0xE7, (uint8_t[]){ 0x44, 0x44 }, 2, 0 },
    { 0xE8, (uint8_t[]){ 0x0C, 0x90, 0xA0, 0xA0, 0x0E, 0x92, 0xA0, 0xA0,
                          0x08, 0x8C, 0xA0, 0xA0, 0x0A, 0x8E, 0xA0, 0xA0 }, 16, 0 },
    { 0xE9, (uint8_t[]){ 0x36, 0x00 }, 2, 0 },
    /* 0xEB: the supplier TXT lists 7 data bytes (matches standard ST7701S refs). */
    { 0xEB, (uint8_t[]){ 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40 }, 7, 0 },
    { 0xED, (uint8_t[]){ 0xFF, 0x45, 0x67, 0xFA, 0x01, 0x2B, 0xCF, 0xFF,
                          0xFF, 0xFC, 0xB2, 0x10, 0xAF, 0x76, 0x54, 0xFF }, 16, 0 },
    { 0xEF, (uint8_t[]){ 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F }, 6, 0 },

    /*
     * Appended vs supplier TXT: force Command2 BK1 self-test OFF, then disable
     * Command2 before Sleep Out / Display On. Matches Espressif reference
     * st7701 init; the supplier TXT was missing both tail blocks and the
     * panel stayed in Command2 BK0 mode forever, which can block DISPON.
     */
    { 0xFF, (uint8_t[]){ 0x77, 0x01, 0x00, 0x00, 0x13 }, 5, 0 },
    { 0xE8, (uint8_t[]){ 0x00, 0x0E },                   2, 0 },
    { 0xE8, (uint8_t[]){ 0x00, 0x0C },                   2, 20 },
    { 0xE8, (uint8_t[]){ 0x00, 0x00 },                   2, 0 },
    { 0xFF, (uint8_t[]){ 0x77, 0x01, 0x00, 0x00, 0x00 }, 5, 0 },

    { 0x11, NULL, 0, 120 },   /* Sleep Out + 120 ms */
    { 0x29, NULL, 0, 0 },     /* Display On */
    { 0x35, (uint8_t[]){ 0x00 }, 1, 0 },  /* Tearing Effect On (V-blank only) */
};

static esp_err_t dphy_power_on(void)
{
    esp_ldo_channel_config_t cfg = {
        .chan_id = BSP_MIPI_DPHY_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DPHY_LDO_MV,
    };
    return esp_ldo_acquire_channel(&cfg, &s_dphy_ldo);
}

esp_err_t bsp_display_st7701_init(void)
{
    ESP_RETURN_ON_ERROR(dphy_power_on(), TAG, "DPHY LDO");

    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_LCD_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus), TAG, "dsi bus");
    ESP_LOGI(TAG, "DSI bus OK (%d lanes @ %d Mbps)", BSP_LCD_DSI_LANES, BSP_LCD_DSI_LANE_MBPS);

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_io), TAG, "dbi io");

    /*
     * DPI timing cloned from BSP 4-inch ST7701 (480x800) which is known to work
     * on Waveshare's reference ST7701 panels. Total H=576, V=710; @25 MHz pclk
     * gives ~61 Hz refresh. ST7701 accepts a wide porch tolerance.
     */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_LCD_DPI_CLK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = DISPLAY_NUM_FBS,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_pulse_width = 12,
            .hsync_back_porch  = 42,
            .hsync_front_porch = 42,
            .vsync_pulse_width = 8,
            .vsync_back_porch  = 2,
            .vsync_front_porch = 60,
        },
        .flags.use_dma2d = true,
    };

    st7701_vendor_config_t vendor_config = {
        .init_cmds = vendor_specific_init,
        .init_cmds_size = sizeof(vendor_specific_init) / sizeof(vendor_specific_init[0]),
        .flags = { .use_mipi_interface = 1 },
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    esp_lcd_panel_dev_config_t panel_dev = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(s_io, &panel_dev, &s_panel), TAG, "new st7701");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    void *fb0 = NULL;
    void *fb1 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, DISPLAY_NUM_FBS, &fb0, &fb1), TAG, "get fb");
    s_fbs[0] = (uint16_t *)fb0;
    s_fbs[1] = (uint16_t *)fb1;
    s_front_fb_index = 0;
    ESP_LOGI(TAG, "ST7701S init OK, fb0=%p fb1=%p, %dx%d RGB565",
             s_fbs[0], s_fbs[1], BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

esp_err_t bsp_display_st7701_fill(uint16_t rgb565)
{
    if (!s_panel || !s_fbs[0] || !s_fbs[1]) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t back_fb_index = s_front_fb_index ^ 1;
    uint16_t *back_fb = s_fbs[back_fb_index];
    size_t px = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES;
    for (size_t i = 0; i < px; ++i) {
        back_fb[i] = rgb565;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, back_fb),
                        TAG, "swap fb");
    s_front_fb_index = back_fb_index;
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_st7701_get_panel(void)
{
    return s_panel;
}

esp_err_t bsp_display_st7701_register_color_trans_done_callback(
    esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx)
{
    if (!s_panel) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = cb,
    };
    return esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, user_ctx);
}
