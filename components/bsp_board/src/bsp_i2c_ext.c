#include "bsp_i2c_ext.h"
#include "bsp_board_pins.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_i2c_ext";

/* 100 kHz for bring-up: safest with GY-302's 4.7k pullups plus internal pullups.
 * Can be raised to BSP_I2C_EXT_FREQ_HZ (400 kHz) once the link is verified. */
#define BOARD_I2C_EXT_FREQ_HZ    100000
#define BOARD_I2C_EXT_MAX_DEVICES 8

typedef struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} bsp_i2c_ext_device_t;

static SemaphoreHandle_t s_mutex = NULL;
static bool s_ready = false;
static i2c_master_bus_handle_t s_bus = NULL;
static bsp_i2c_ext_device_t s_devices[BOARD_I2C_EXT_MAX_DEVICES] = {0};

static int timeout_ms_from_ticks(TickType_t timeout)
{
    return timeout == portMAX_DELAY ? -1 : (int)pdTICKS_TO_MS(timeout);
}

static esp_err_t get_device_locked(uint8_t addr, i2c_master_dev_handle_t *out_handle)
{
    for (size_t i = 0; i < BOARD_I2C_EXT_MAX_DEVICES; ++i) {
        if (s_devices[i].handle && s_devices[i].addr == addr) {
            *out_handle = s_devices[i].handle;
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < BOARD_I2C_EXT_MAX_DEVICES; ++i) {
        if (!s_devices[i].handle) {
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addr,
                .scl_speed_hz = BOARD_I2C_EXT_FREQ_HZ,
            };
            ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg,
                                                          &s_devices[i].handle),
                                TAG, "add dev 0x%02X", addr);
            s_devices[i].addr = addr;
            *out_handle = s_devices[i].handle;
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

esp_err_t bsp_i2c_ext_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "create mutex");
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_EXT_PORT,
        .sda_io_num = BSP_I2C_EXT_SDA,
        .scl_io_num = BSP_I2C_EXT_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "new i2c ext bus");

    s_ready = true;
    ESP_LOGI(TAG, "I2C_EXT ready on port %d (SCL=%d SDA=%d %d Hz)",
             BSP_I2C_EXT_PORT, BSP_I2C_EXT_SCL, BSP_I2C_EXT_SDA, BOARD_I2C_EXT_FREQ_HZ);
    return ESP_OK;
}

esp_err_t bsp_i2c_ext_lock(TickType_t timeout)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_mutex, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bsp_i2c_ext_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t bsp_i2c_ext_write(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                              TickType_t timeout)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_ext_lock(timeout), TAG, "lock");
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = get_device_locked(addr, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit(dev, write_buf, write_len, timeout_ms_from_ticks(timeout));
    }
    bsp_i2c_ext_unlock();
    return err;
}

esp_err_t bsp_i2c_ext_read(uint8_t addr, uint8_t *read_buf, size_t read_len,
                             TickType_t timeout)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_ext_lock(timeout), TAG, "lock");
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = get_device_locked(addr, &dev);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev, read_buf, read_len, timeout_ms_from_ticks(timeout));
    }
    bsp_i2c_ext_unlock();
    return err;
}

esp_err_t bsp_i2c_ext_write_read(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                                   uint8_t *read_buf, size_t read_len, TickType_t timeout)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_ext_lock(timeout), TAG, "lock");
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = get_device_locked(addr, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(dev, write_buf, write_len, read_buf, read_len,
                                          timeout_ms_from_ticks(timeout));
    }
    bsp_i2c_ext_unlock();
    return err;
}

i2c_port_t bsp_i2c_ext_port(void)
{
    return BSP_I2C_EXT_PORT;
}

i2c_master_bus_handle_t bsp_i2c_ext_get_handle(void)
{
    return s_bus;
}

esp_err_t bsp_i2c_ext_probe(uint8_t addr, bool *present, TickType_t timeout)
{
    if (!present) {
        return ESP_ERR_INVALID_ARG;
    }
    *present = false;

    ESP_RETURN_ON_ERROR(bsp_i2c_ext_lock(timeout), TAG, "lock");
    esp_err_t err = i2c_master_probe(s_bus, addr, timeout_ms_from_ticks(timeout));
    bsp_i2c_ext_unlock();

    if (err == ESP_OK) {
        *present = true;
        return ESP_OK;
    }
    /* No-ACK on probe is the normal "not present" outcome; surface as ESP_OK with present=false. */
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    return err;
}
