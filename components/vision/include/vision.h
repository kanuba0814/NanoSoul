#pragma once
/*
 * vision — local face perception. Runs ESP-DL HumanFaceDetect on the camera's
 * detector-input frames (off the capture task) and turns each detection into a
 * face observation the soul state machine consumes: normalized center offset,
 * bbox area ratio (a distance proxy), and a frontal score (from the eye/nose
 * keypoints). Fully local — no cloud in the perception loop.
 *
 * Implemented in C++ (esp-dl is C++); this header is the C facade.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry.h"   // tel_face_t is the shared observation shape

#ifdef __cplusplus
extern "C" {
#endif

// Create the detector and subscribe to camera frames. Camera must be inited.
esp_err_t vision_init(void);
// Begin detecting (camera must be started).
esp_err_t vision_start(void);

// Latest observation (thread-safe copy).
void      vision_get(tel_face_t *out);
// Detections completed since boot, and recent detection fps.
uint32_t  vision_detect_count(void);
float     vision_fps(void);
bool      vision_ready(void);

#ifdef __cplusplus
}
#endif
