#include "bsp_board.h"

static bsp_board_status_t s_status = {
    .display_ready = false,
    .touch_ready = false,
    .camera_ready = false,
    .audio_ready = false,
    .storage_ready = false,
    .wireless_ready = false,
};

esp_err_t bsp_board_init(void)
{
    s_status.display_ready = true;
    s_status.touch_ready = true;
    s_status.camera_ready = true;
    s_status.audio_ready = true;
    s_status.storage_ready = true;
    s_status.wireless_ready = true;
    return ESP_OK;
}

const bsp_board_status_t *bsp_board_get_status(void)
{
    return &s_status;
}

