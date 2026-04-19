#include "diag_core.h"

static diag_health_state_t s_health = DIAG_HEALTH_OK;

esp_err_t diag_core_init(void)
{
    s_health = DIAG_HEALTH_OK;
    return ESP_OK;
}

diag_health_state_t diag_core_get_health(void)
{
    return s_health;
}

