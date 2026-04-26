#pragma once

#include "esp_err.h"
#include "motion_core_types.h"

esp_err_t motion_core_init(void);
motion_state_t motion_core_get_state(void);
esp_err_t motion_core_request(const motion_request_t *request);
