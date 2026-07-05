#pragma once
/*
 * camera — OV5647 MIPI-CSI capture via esp_video (V4L2), ported from the working
 * /home/gxxl/testP4 bring-up. Each captured frame is PPA-downscaled (full FOV,
 * no crop) to a detector-input frame and handed to a registered callback; the
 * vision component runs face detection on it off the capture task.
 *
 * The CSI DPHY LDO must be pinned to the shared MIPI rail — see
 * cmake/patch_esp_video_ldo.cmake.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Detector-input frame size. Aspect matches the ~0.625 sensor portrait so the
 * face model sees an undistorted image. */
#define CAMERA_DET_W 300
#define CAMERA_DET_H 480

typedef void (*camera_frame_cb_t)(const uint16_t *rgb565, int w, int h, void *ctx);

esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus);
esp_err_t camera_start(void);
esp_err_t camera_stop(void);
void      camera_register_frame_cb(camera_frame_cb_t cb, void *ctx);
bool      camera_running(void);
void      camera_sensor_wh(int *w, int *h);
uint32_t  camera_frame_count(void);

// HW-JPEG-encode the latest detector frame. Caller frees *out. For the
// companion snapshot command.
esp_err_t camera_snapshot_jpeg(uint8_t **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
