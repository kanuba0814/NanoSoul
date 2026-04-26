#include "drivers/bh1750.h"

#include "bsp_i2c_ext.h"
#include "bsp_board_pins.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ambient_light";

#define BH1750_CMD_POWER_ON              0x01
#define BH1750_CMD_CONT_H_RES_MODE       0x10
#define BH1750_MEASUREMENT_MAX_MS        180
#define BH1750_I2C_TIMEOUT_MS            20
#define BH1750_ADDR_ALT                  0x5C

static bool s_sensor_ready = false;
static uint8_t s_sensor_addr = BSP_BH1750_ADDR;
static TickType_t s_measurement_started_at = 0;

static esp_err_t probe_addr(uint8_t addr)
{
    const TickType_t timeout = pdMS_TO_TICKS(BH1750_I2C_TIMEOUT_MS);
    bool present = false;
    esp_err_t err = bsp_i2c_ext_probe(addr, &present, timeout);
    if (err != ESP_OK) {
        return err;
    }
    return present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void scan_bus(void)
{
    bool found = false;

    ESP_LOGI(TAG, "scan I2C_EXT port %d start", bsp_i2c_ext_port());
    for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
        if (probe_addr(addr) == ESP_OK) {
            ESP_LOGI(TAG, "scan I2C_EXT found addr 0x%02X", addr);
            found = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "scan I2C_EXT found no devices");
    }
}

static esp_err_t detect_sensor(void)
{
    const uint8_t candidates[] = { BSP_BH1750_ADDR, BH1750_ADDR_ALT };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        esp_err_t err = probe_addr(candidates[i]);
        if (err == ESP_OK) {
            s_sensor_addr = candidates[i];
            ESP_LOGI(TAG, "BH1750 detected on I2C_EXT addr 0x%02X", s_sensor_addr);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "probe I2C_EXT addr 0x%02X -> %s", candidates[i], esp_err_to_name(err));
    }

    scan_bus();
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t write_cmd(uint8_t cmd)
{
    const TickType_t timeout = pdMS_TO_TICKS(BH1750_I2C_TIMEOUT_MS);
    esp_err_t err = bsp_i2c_ext_write(s_sensor_addr, &cmd, sizeof(cmd), timeout);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write cmd 0x%02X to addr 0x%02X failed: %s",
                 cmd, s_sensor_addr, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t read_raw(uint16_t *raw)
{
    uint8_t data[2] = {0};
    const TickType_t timeout = pdMS_TO_TICKS(BH1750_I2C_TIMEOUT_MS);

    esp_err_t err = bsp_i2c_ext_read(s_sensor_addr, data, sizeof(data), timeout);
    ESP_RETURN_ON_ERROR(err, TAG, "read raw");

    *raw = (uint16_t)((data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t bh1750_init(void)
{
    if (s_sensor_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(detect_sensor(), TAG, "detect sensor");
    ESP_RETURN_ON_ERROR(write_cmd(BH1750_CMD_POWER_ON), TAG, "power on");
    ESP_RETURN_ON_ERROR(write_cmd(BH1750_CMD_CONT_H_RES_MODE), TAG,
                        "start continuous measurement");

    s_measurement_started_at = xTaskGetTickCount();
    s_sensor_ready = true;
    ESP_LOGI(TAG, "BH1750 started on port %d addr 0x%02X", bsp_i2c_ext_port(), s_sensor_addr);
    return ESP_OK;
}

esp_err_t bh1750_read(bh1750_sample_t *sample)
{
    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_sensor_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    sample->addr = s_sensor_addr;
    sample->raw = 0;
    sample->lux = 0.0f;

    TickType_t elapsed = xTaskGetTickCount() - s_measurement_started_at;
    if (elapsed < pdMS_TO_TICKS(BH1750_MEASUREMENT_MAX_MS)) {
        return ESP_ERR_NOT_FINISHED;
    }

    ESP_RETURN_ON_ERROR(read_raw(&sample->raw), TAG, "sample");
    sample->lux = sample->raw / 1.2f;
    return ESP_OK;
}

uint8_t bh1750_addr(void)
{
    return s_sensor_addr;
}
