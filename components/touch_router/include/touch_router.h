#pragma once
/*
 * touch_router — the single owner/reader of the FT6x36 touch panel. One 20 Hz
 * poll task holds the only device handle; it caches the latest point and posts
 * NS_EVT_TOUCH on a press edge (with long_press when held past ~800ms). Every
 * other consumer (soul, the debug UI) reads the cache — never touch_read_raw
 * directly — so there is exactly one reader on the shared I2C0 bus.
 */

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "touch.h"   /* touch_point_t */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_router_init(i2c_master_bus_handle_t bus);   /* init panel + start poll */
bool      touch_router_ready(void);
void      touch_router_last(touch_point_t *pt, bool *pressed);  /* cached latest */

#ifdef __cplusplus
}
#endif
