#pragma once

#include "diag_core_types.h"
#include "esp_err.h"

esp_err_t diag_core_init(void);
diag_health_state_t diag_core_get_health(void);

