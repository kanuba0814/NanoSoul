#pragma once

#include "app_core_types.h"
#include "esp_err.h"

esp_err_t app_core_init(void);
void app_core_start_loop(void);
app_mode_t app_core_get_mode(void);

