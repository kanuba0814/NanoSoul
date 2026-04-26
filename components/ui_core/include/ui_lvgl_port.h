#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_lvgl_port_init(void);
esp_err_t ui_lvgl_port_start(void);
lv_display_t *ui_lvgl_port_get_display(void);
void ui_lvgl_port_lock(void);
void ui_lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
