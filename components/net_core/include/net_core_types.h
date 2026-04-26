#pragma once

typedef enum {
    NET_WIFI_UNCONFIGURED = 0,
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_WIFI_ERROR,
} net_wifi_state_t;

typedef enum {
    NET_CLOUD_UNAVAILABLE = 0,
    NET_CLOUD_CONNECTING,
    NET_CLOUD_AVAILABLE,
    NET_CLOUD_ERROR,
} net_cloud_state_t;

typedef enum {
    NET_BLE_PROVISIONING_DISABLED = 0,
    NET_BLE_PROVISIONING_READY,
} net_ble_state_t;
