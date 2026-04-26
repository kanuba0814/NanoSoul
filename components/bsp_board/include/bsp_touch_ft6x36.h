#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
} bsp_touch_point_t;

esp_err_t bsp_touch_ft6x36_init(void);
esp_err_t bsp_touch_ft6x36_read_point(bsp_touch_point_t *pt, bool *pressed);
void bsp_touch_ft6x36_set_transform(bool swap_xy, bool mirror_x, bool mirror_y);
