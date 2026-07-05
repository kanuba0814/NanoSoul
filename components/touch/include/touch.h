#pragma once
/*
 * touch — FocalTech FT6x36 capacitive touch on board I2C0 (addr 0x38).
 *
 * Ported from the working /home/gxxl/testP4 bring-up (raw register map, no
 * esp_lcd_touch layer) so the debug UI can do its own hit-testing in canvas
 * coordinates. Returns RAW chip coordinates; the caller applies whatever
 * rotation/mirror the (90°-rotated) panel needs — see debugui.
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x, y;   /* raw FT6x36 coordinates (pre-transform) */
} touch_point_t;

// Attach the FT6x36 to the shared board I2C0 bus and verify it responds.
esp_err_t touch_init(i2c_master_bus_handle_t bus);
bool      touch_ready(void);

// Sample the current primary touch. *pressed = finger down; pt = raw coords.
// Returns ESP_OK even when not pressed (pt then undefined).
esp_err_t touch_read_raw(touch_point_t *pt, bool *pressed);

#ifdef __cplusplus
}
#endif
