#include "light.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ns_config.h"
#include "telemetry.h"

static const char *TAG = "light";

#define BH1750_ADDR       0x23
#define BH1750_POWER_ON   0x01
#define BH1750_CONT_HRES  0x10   /* continuous high-res, 1 lux, ~120ms */
#define BH1750_LSB_DIV    1.2f   /* raw / 1.2 = lux */

static i2c_master_dev_handle_t s_dev;
static bool  s_present;
static float s_lux;

static float read_lux(void)
{
    uint8_t rx[2] = {0};
    if (!s_dev || i2c_master_receive(s_dev, rx, 2, 100) != ESP_OK) {
        return s_lux;   /* keep last on a hiccup */
    }
    uint16_t raw = (uint16_t)((rx[0] << 8) | rx[1]);
    return (float)raw / BH1750_LSB_DIV;
}

static void light_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    bool    dark = false;
    int64_t dark_since = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_lux = read_lux();
        int64_t now = esp_timer_get_time() / 1000;

        if (!dark) {
            if (s_lux < (float)cfg->light.dark_lux) {
                if (dark_since == 0) {
                    dark_since = now;
                } else if (now - dark_since >= (int64_t)cfg->light.dark_hold_s * 1000) {
                    dark = true;
                    telemetry_post(NS_EVT_DARK, NULL, 0);
                    ESP_LOGI(TAG, "DARK (%.0f lux)", s_lux);
                }
            } else {
                dark_since = 0;
            }
        } else {
            if (s_lux > (float)cfg->light.bright_lux) {
                dark = false;
                dark_since = 0;
                telemetry_post(NS_EVT_BRIGHT, NULL, 0);
                ESP_LOGI(TAG, "BRIGHT (%.0f lux)", s_lux);
            }
        }
    }
}

esp_err_t light_init(i2c_master_bus_handle_t bus)
{
    if (!bus || i2c_master_probe(bus, BH1750_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 @0x%02X 未探到 → 无环境光", BH1750_ADDR);
        return ESP_OK;   /* absent, graceful */
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "add device failed");
        return ESP_OK;
    }
    uint8_t on = BH1750_POWER_ON, mode = BH1750_CONT_HRES;
    i2c_master_transmit(s_dev, &on, 1, 100);
    i2c_master_transmit(s_dev, &mode, 1, 100);
    s_present = true;
    ESP_LOGI(TAG, "BH1750 online @0x%02X", BH1750_ADDR);
    xTaskCreatePinnedToCore(light_task, "light", 2560, NULL, 3, NULL, 0);
    return ESP_OK;
}

bool  light_present(void) { return s_present; }
float light_lux(void)     { return s_lux; }
