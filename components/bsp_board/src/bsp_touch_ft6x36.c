#include "bsp_touch_ft6x36.h"
#include "bsp_i2c.h"
#include "bsp_board_pins.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "touch";

/*
 * FocalTech FT6x36 / FT5x06 register map (capacitive multi-touch).
 *   0x00   DEV_MODE
 *   0x01   GEST_ID
 *   0x02   TD_STATUS        — low 4 bits = current touch count
 *   0x03   P1_XH            — bit7..6: event (0=down, 1=up, 2=contact), bit3..0: X high nibble
 *   0x04   P1_XL
 *   0x05   P1_YH            — bit3..0: Y high nibble
 *   0x06   P1_YL
 *   0xA3   CHIPID           — 0x06/0x36/0x64 depending on variant
 *   0xA8   VENDID           — typically 0x11 for FocalTech
 */
#define FT_ADDR             0x38
#define FT_REG_CHIPID       0xA3
#define FT_REG_VENDID       0xA8
#define FT_REG_TD_STATUS    0x02
#define I2C_TIMEOUT_MS      20

static bool s_ready = false;
static bool s_swap_xy = BSP_TOUCH_SWAP_XY;
static bool s_mirror_x = BSP_TOUCH_MIRROR_X;
static bool s_mirror_y = BSP_TOUCH_MIRROR_Y;

static esp_err_t ft_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return bsp_i2c_write_read(FT_ADDR, &reg, 1, buf, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static void dump_region(const char *label, uint8_t start, uint8_t len)
{
    uint8_t buf[16] = {0};
    if (len > sizeof(buf)) len = sizeof(buf);
    if (ft_read(start, buf, len) != ESP_OK) {
        ESP_LOGW(TAG, "  %s read failed", label);
        return;
    }
    char line[80];
    int n = snprintf(line, sizeof(line), "  %s 0x%02X:", label, start);
    for (int i = 0; i < len; ++i) {
        n += snprintf(line + n, sizeof(line) - n, " %02X", buf[i]);
    }
    ESP_LOGI(TAG, "%s", line);
}

esp_err_t bsp_touch_ft6x36_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");

    uint8_t dummy;
    esp_err_t err = ft_read(0x00, &dummy, 1);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t chip_id = 0;
    uint8_t vend_id = 0;
    ft_read(FT_REG_CHIPID, &chip_id, 1);
    ft_read(FT_REG_VENDID, &vend_id, 1);

    s_ready = true;
    ESP_LOGI(TAG, "touch @ 0x%02X responding, dumping signature regions:", FT_ADDR);
    ESP_LOGI(TAG, "touch chip_id=0x%02X vend_id=0x%02X", chip_id, vend_id);
    dump_region("status",  0x00, 16);
    dump_region("id_ft",   0xA0, 16);
    return ESP_OK;
}

void bsp_touch_ft6x36_set_transform(bool swap_xy, bool mirror_x, bool mirror_y)
{
    s_swap_xy = swap_xy;
    s_mirror_x = mirror_x;
    s_mirror_y = mirror_y;
}

static void clamp_and_transform(uint16_t *x, uint16_t *y)
{
    uint16_t tx = *x;
    uint16_t ty = *y;
    const uint16_t max_x = BSP_LCD_H_RES - 1;
    const uint16_t max_y = BSP_LCD_V_RES - 1;

    if (s_swap_xy) {
        uint16_t tmp = tx;
        tx = ty;
        ty = tmp;
    }

    if (tx > max_x) tx = max_x;
    if (ty > max_y) ty = max_y;

    if (s_mirror_x) tx = max_x - tx;
    if (s_mirror_y) ty = max_y - ty;

    *x = tx;
    *y = ty;
}

esp_err_t bsp_touch_ft6x36_read_point(bsp_touch_point_t *pt, bool *pressed)
{
    if (!pt || !pressed) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        *pressed = false;
        pt->x = 0;
        pt->y = 0;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t frame[7] = {0};
    esp_err_t err = ft_read(FT_REG_TD_STATUS, frame, sizeof(frame));
    if (err != ESP_OK) {
        *pressed = false;
        pt->x = 0;
        pt->y = 0;
        return err;
    }

    uint8_t n = frame[0] & 0x0F;
    if (n == 0 || n > 2) {
        *pressed = false;
        pt->x = 0;
        pt->y = 0;
        return ESP_OK;
    }

    uint8_t event = (frame[1] >> 6) & 0x03;
    uint16_t x = (uint16_t)(((frame[1] & 0x0F) << 8) | frame[2]);
    uint16_t y = (uint16_t)(((frame[3] & 0x0F) << 8) | frame[4]);

    clamp_and_transform(&x, &y);

    pt->x = x;
    pt->y = y;
    *pressed = (event == 0 || event == 2);
    return ESP_OK;
}
