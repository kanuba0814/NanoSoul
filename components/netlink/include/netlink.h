#pragma once
/*
 * netlink — LAN connectivity over the on-board ESP32-C6 (Wi-Fi 6 via SDIO +
 * esp_hosted; the standard esp_wifi API is transparently routed to the C6).
 * Connects STA using the SD config credentials, auto-reconnects, publishes net
 * state to telemetry, and starts SNTP + mDNS (nanosoul.local) once online.
 *
 * The companion WebSocket server (Phase F) is added to this component later.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the Wi-Fi STA (always, even with no SD credentials so the UI can
// scan/connect), then auto-connect if ns_config has an SSID.
esp_err_t netlink_start(void);

bool netlink_is_up(void);
void netlink_get_ip(char *ip, size_t n);

// One AP from a scan.
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t authmode;   // wifi_auth_mode_t; WIFI_AUTH_OPEN == 0 => no password
} netlink_ap_t;

// Blocking scan (~2-4s). Fills up to `max` APs (strongest first), *count set.
esp_err_t netlink_scan(netlink_ap_t *out, int max, int *count);

// Connect to a specific AP (from the on-screen UI). Brings STA up if needed.
esp_err_t netlink_connect(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
