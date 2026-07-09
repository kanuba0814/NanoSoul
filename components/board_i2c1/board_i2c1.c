#include "board_i2c1.h"

#include "driver/gpio.h"
#include "esp_log.h"

/* Off-board sensor bus. Pins are dupont-wired and may change; keep in sync with
 * CLAUDE.md firmware section (I²C1 SDA=IO20 / SCL=IO21). */
#define I2C1_PORT   I2C_NUM_1
#define I2C1_SDA    GPIO_NUM_20
#define I2C1_SCL    GPIO_NUM_21

static const char *TAG = "board_i2c1";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c1_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C1_PORT,
        .sda_io_num = I2C1_SDA,
        .scl_io_num = I2C1_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c1 bus create failed: %s", esp_err_to_name(err));
        s_bus = NULL;
    } else {
        ESP_LOGI(TAG, "I2C1 up (SDA=%d SCL=%d)", I2C1_SDA, I2C1_SCL);
    }
    return err;
}

i2c_master_bus_handle_t board_i2c1_bus(void)
{
    return s_bus;
}

bool board_i2c1_probe(uint8_t addr)
{
    return s_bus && i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}
