#include "net_core.h"

static net_wifi_state_t s_wifi_state = NET_WIFI_UNCONFIGURED;

esp_err_t net_core_init(void)
{
    s_wifi_state = NET_WIFI_UNCONFIGURED;
    return ESP_OK;
}

net_wifi_state_t net_core_get_wifi_state(void)
{
    return s_wifi_state;
}

