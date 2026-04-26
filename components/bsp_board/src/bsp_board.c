#include "bsp_board.h"

#include "bsp_board_pins.h"
#include "bsp_display_st7701.h"
#include "bsp_i2c.h"
#include "bsp_i2c_ext.h"
#include "bsp_touch_ft6x36.h"

#include "esp_log.h"

static const char *TAG = "bsp_board";

static bsp_board_status_t s_status = {
    .display = HW_STATUS_ABSENT,
    .touch = HW_STATUS_ABSENT,
    .audio = HW_STATUS_ABSENT,
    .storage = HW_STATUS_ABSENT,
    .camera = HW_STATUS_ABSENT,
    .wifi = HW_STATUS_DISABLED,
    .i2c_board = HW_STATUS_ABSENT,
    .i2c_ext = HW_STATUS_ABSENT,
    .bh1750 = HW_STATUS_ABSENT,
    .vl6180x_l = HW_STATUS_ABSENT,
    .vl6180x_c = HW_STATUS_ABSENT,
    .vl6180x_r = HW_STATUS_ABSENT,
    .motion = HW_STATUS_DISABLED,
};

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "init %s (%s)", BSP_GPIO_MAP_NAME, BSP_GPIO_MAP_STATUS);

    esp_err_t err = bsp_i2c_init();
    s_status.i2c_board = err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "board I2C init failed: %s", esp_err_to_name(err));
    }

    err = bsp_i2c_ext_init();
    s_status.i2c_ext = err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "external I2C init failed: %s", esp_err_to_name(err));
    }

    err = bsp_display_st7701_init();
    s_status.display = err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_touch_ft6x36_init();
    s_status.touch = err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

const bsp_board_status_t *bsp_board_get_status(void)
{
    return &s_status;
}

const char *bsp_board_hw_status_name(hw_status_t status)
{
    switch (status) {
    case HW_STATUS_ABSENT:
        return "ABSENT";
    case HW_STATUS_OK:
        return "OK";
    case HW_STATUS_STALE:
        return "STALE";
    case HW_STATUS_ERROR:
        return "ERROR";
    case HW_STATUS_DISABLED:
        return "DISABLED";
    default:
        return "UNKNOWN";
    }
}

hw_status_t bsp_board_get_display_status(void)
{
    return s_status.display;
}

hw_status_t bsp_board_get_touch_status(void)
{
    return s_status.touch;
}

hw_status_t bsp_board_get_audio_status(void)
{
    return s_status.audio;
}

hw_status_t bsp_board_get_storage_status(void)
{
    return s_status.storage;
}

hw_status_t bsp_board_get_camera_status(void)
{
    return s_status.camera;
}

hw_status_t bsp_board_get_wifi_status(void)
{
    return s_status.wifi;
}

void bsp_board_set_audio_status(hw_status_t status)
{
    s_status.audio = status;
}

void bsp_board_set_storage_status(hw_status_t status)
{
    s_status.storage = status;
}

void bsp_board_set_camera_status(hw_status_t status)
{
    s_status.camera = status;
}

void bsp_board_set_wifi_status(hw_status_t status)
{
    s_status.wifi = status;
}

void bsp_board_set_bh1750_status(hw_status_t status)
{
    s_status.bh1750 = status;
}
