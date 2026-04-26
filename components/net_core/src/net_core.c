#include "net_core.h"

#include "bsp_board.h"
#include "net_wifi.h"

static net_wifi_state_t s_wifi_state = NET_WIFI_UNCONFIGURED;
static net_cloud_state_t s_cloud_state = NET_CLOUD_UNAVAILABLE;

esp_err_t net_core_init(void)
{
    esp_err_t err = app_wifi_init();
    s_wifi_state = err == ESP_OK ? NET_WIFI_UNCONFIGURED : NET_WIFI_ERROR;
    s_cloud_state = NET_CLOUD_UNAVAILABLE;
    bsp_board_set_wifi_status(err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR);
    return err;
}

net_wifi_state_t net_core_get_wifi_state(void)
{
    return s_wifi_state;
}

net_cloud_state_t net_core_get_cloud_state(void)
{
    return s_cloud_state;
}
