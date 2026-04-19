#include "hal_mock.h"

static hal_mock_scenario_t s_scenario = HAL_MOCK_SCENARIO_NONE;

esp_err_t hal_mock_init(void)
{
    s_scenario = HAL_MOCK_SCENARIO_NONE;
    return ESP_OK;
}

hal_mock_scenario_t hal_mock_get_scenario(void)
{
    return s_scenario;
}

