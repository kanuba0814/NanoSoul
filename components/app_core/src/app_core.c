#include "app_core.h"

static app_mode_t s_app_mode = APP_MODE_BOOT;

esp_err_t app_core_init(void)
{
    s_app_mode = APP_MODE_IDLE;
    return ESP_OK;
}

void app_core_start_loop(void)
{
}

app_mode_t app_core_get_mode(void)
{
    return s_app_mode;
}

