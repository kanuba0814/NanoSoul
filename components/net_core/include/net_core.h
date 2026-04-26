#pragma once

#include "esp_err.h"
#include "net_core_types.h"

esp_err_t net_core_init(void);
net_wifi_state_t net_core_get_wifi_state(void);
net_cloud_state_t net_core_get_cloud_state(void);
