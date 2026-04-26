#include "net_core.h"

static net_wifi_state_t s_wifi_state = NET_WIFI_UNCONFIGURED;
static net_cloud_state_t s_cloud_state = NET_CLOUD_UNAVAILABLE;

esp_err_t net_core_init(void)
{
    s_wifi_state = NET_WIFI_UNCONFIGURED;
    s_cloud_state = NET_CLOUD_UNAVAILABLE;
    return ESP_OK;
}

net_wifi_state_t net_core_get_wifi_state(void)
{
    return s_wifi_state;
}

net_cloud_state_t net_core_get_cloud_state(void)
{
    return s_cloud_state;
}
