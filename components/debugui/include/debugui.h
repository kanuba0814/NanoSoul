#pragma once
/*
 * debugui — touch-driven hidden debug UI on top of the emote face.
 *
 * Entry gesture: 10 rapid taps on the screen opens the debug menu (which then
 * hosts WiFi control + a live camera preview page). Touch is the only input.
 *
 * This first stage brings up the FT6x36 touch driver, detects the 10-tap
 * gesture, and logs raw touch coordinates (so the panel-rotation transform can
 * be calibrated) with on-screen tap feedback. The menu/camera/keyboard pages
 * are layered on next.
 */

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t debugui_init(i2c_master_bus_handle_t bus);
esp_err_t debugui_start(void);
bool      debugui_touch_ok(void);

#ifdef __cplusplus
}
#endif
