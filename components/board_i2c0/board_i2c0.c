#include "board_i2c0.h"
#include "bsp_pins.h"

#include "esp_log.h"

static const char *TAG = "board_i2c0";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c0_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C0_PORT,
        .sda_io_num = BSP_I2C0_SDA,
        .scl_io_num = BSP_I2C0_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c0 bus create failed: %s", esp_err_to_name(err));
        s_bus = NULL;
    } else {
        ESP_LOGI(TAG, "I2C0 up (SDA=%d SCL=%d)", BSP_I2C0_SDA, BSP_I2C0_SCL);
    }
    return err;
}

i2c_master_bus_handle_t board_i2c0_bus(void)
{
    return s_bus;
}

bool board_i2c0_probe(uint8_t addr)
{
    return s_bus && i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}
