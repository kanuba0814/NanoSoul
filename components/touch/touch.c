#include "touch.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "touch";

/*
 * FT6x36 / FT5x06 register map:
 *   0x02  TD_STATUS  — low 4 bits = touch count
 *   0x03  P1_XH      — bit7..6 event (0=down,1=up,2=contact), bit3..0 X[11:8]
 *   0x04  P1_XL      — X[7:0]
 *   0x05  P1_YH      — bit3..0 Y[11:8]
 *   0x06  P1_YL      — Y[7:0]
 *   0xA3  CHIPID     0xA8 VENDID (signature)
 */
#define FT_ADDR          0x38
#define FT_REG_TD_STATUS 0x02
#define FT_REG_CHIPID    0xA3
#define FT_REG_VENDID    0xA8
#define FT_I2C_HZ        400000
#define FT_TIMEOUT_MS    20

static i2c_master_dev_handle_t s_dev;
static bool                    s_ready;

static esp_err_t ft_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, FT_TIMEOUT_MS);
}

esp_err_t touch_init(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "no bus");

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = FT_ADDR,
        .scl_speed_hz    = FT_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dc, &s_dev), TAG, "add dev");

    uint8_t dummy;
    esp_err_t err = ft_read(0x00, &dummy, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6x36 @0x%02x not responding: %s", FT_ADDR, esp_err_to_name(err));
        return err;
    }
    uint8_t chip = 0, vend = 0;
    ft_read(FT_REG_CHIPID, &chip, 1);
    ft_read(FT_REG_VENDID, &vend, 1);
    s_ready = true;
    ESP_LOGI(TAG, "FT6x36 up: chip_id=0x%02x vend_id=0x%02x", chip, vend);
    return ESP_OK;
}

bool touch_ready(void)
{
    return s_ready;
}

esp_err_t touch_read_raw(touch_point_t *pt, bool *pressed)
{
    if (!pt || !pressed) {
        return ESP_ERR_INVALID_ARG;
    }
    *pressed = false;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t f[7] = {0};
    esp_err_t err = ft_read(FT_REG_TD_STATUS, f, sizeof(f));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t n = f[0] & 0x0F;
    if (n == 0 || n > 2) {
        return ESP_OK;   /* no / bogus contact */
    }
    uint8_t event = (f[1] >> 6) & 0x03;
    pt->x = (uint16_t)(((f[1] & 0x0F) << 8) | f[2]);
    pt->y = (uint16_t)(((f[3] & 0x0F) << 8) | f[4]);
    *pressed = (event == 0 || event == 2);   /* down or contact */
    return ESP_OK;
}
