#pragma once

#include "esp_err.h"
#include "hal_mock_types.h"

esp_err_t hal_mock_init(void);
hal_mock_scenario_t hal_mock_get_scenario(void);

