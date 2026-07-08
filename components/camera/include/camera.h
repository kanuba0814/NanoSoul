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

/* Detector-input frame size for an UPRIGHT (0°/180°) sensor mount. Aspect
 * matches the ~0.625 sensor portrait so the face model sees an undistorted
 * image. With a 90°/270° mount the detector frame is the transpose (480x300);
 * same pixel count either way — size buffers with CAMERA_DET_PX and use
 * camera_det_wh() / the callback's w/h for geometry. */
#define CAMERA_DET_W  300
#define CAMERA_DET_H  480
#define CAMERA_DET_PX (CAMERA_DET_W * CAMERA_DET_H)

typedef void (*camera_frame_cb_t)(const uint16_t *rgb565, int w, int h, void *ctx);

/* Physical mount correction, applied in the PPA pass. Call BEFORE camera_start()
 * (typically right before camera_init). deg ∈ {0, 90, 180, 270}; anything else
 * is rejected. 90/270 transpose the detector frame to 480x300. */
esp_err_t camera_set_rotation(int deg);
/* Detector frame dims after the rotation setting (w,h). */
void      camera_det_wh(int *w, int *h);

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

// Copy the latest detector frame (camera_det_wh() dims, RGB565) into a caller
// buffer for on-screen preview. Copies up to dst_px pixels. A minor tear
// against the capture task is cosmetic.
esp_err_t camera_copy_latest(uint16_t *dst, size_t dst_px);

#ifdef __cplusplus
}
#endif
