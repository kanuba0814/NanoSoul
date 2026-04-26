#pragma once

#include "bsp_board_types.h"
#include "diag_core_types.h"
#include "esp_err.h"

esp_err_t diag_core_init(void);
diag_health_state_t diag_core_get_health(void);
const bsp_board_status_t *diag_core_get_board_status(void);
const char *diag_core_hw_status_name(hw_status_t status);
