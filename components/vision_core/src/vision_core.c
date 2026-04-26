#include "vision_core.h"

#include "bsp_board.h"
#include "bsp_i2c.h"

static vision_presence_snapshot_t s_snapshot = {
    .presence = PRESENCE_STATE_ABSENT,
    .user_present = false,
    .x_offset = 0,
    .confidence = 0,
    .timestamp_ms = 0,
};

esp_err_t vision_core_init(void)
{
    esp_err_t err = camera_init(bsp_i2c_get_handle());
    bsp_board_set_camera_status(err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR);
    return err;
}

vision_presence_snapshot_t vision_core_get_snapshot(void)
{
    return s_snapshot;
}

esp_err_t vision_core_camera_start_preview(void)
{
    esp_err_t err = camera_init(bsp_i2c_get_handle());
    if (err != ESP_OK) {
        bsp_board_set_camera_status(HW_STATUS_ERROR);
        return err;
    }
    bsp_board_set_camera_status(HW_STATUS_OK);
    return camera_start_preview();
}

esp_err_t vision_core_camera_stop_preview(void)
{
    return camera_stop_preview();
}

esp_err_t vision_core_camera_capture_jpeg(const char *path)
{
    return camera_capture_jpeg(path);
}

esp_err_t vision_core_camera_register_preview_cb(camera_frame_cb_t cb, void *user_ctx)
{
    return camera_register_preview_cb(cb, user_ctx);
}
