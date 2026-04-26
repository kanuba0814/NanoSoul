#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAMERA_PREVIEW_W 240
#define CAMERA_PREVIEW_H 240

typedef void (*camera_frame_cb_t)(const uint16_t *rgb565, size_t len, void *user_ctx);

esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus);
esp_err_t camera_start_preview(void);
esp_err_t camera_stop_preview(void);
esp_err_t camera_deinit(void);
esp_err_t camera_register_preview_cb(camera_frame_cb_t cb, void *user_ctx);
esp_err_t camera_capture_jpeg(const char *path);

#ifdef __cplusplus
}
#endif
