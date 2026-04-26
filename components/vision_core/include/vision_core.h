#pragma once

#include "esp_err.h"
#include "vision_camera.h"
#include "vision_core_types.h"

esp_err_t vision_core_init(void);
vision_presence_snapshot_t vision_core_get_snapshot(void);
esp_err_t vision_core_camera_start_preview(void);
esp_err_t vision_core_camera_stop_preview(void);
esp_err_t vision_core_camera_capture_jpeg(const char *path);
esp_err_t vision_core_camera_register_preview_cb(camera_frame_cb_t cb, void *user_ctx);
