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

// Bring up STA and start connecting using ns_config wifi credentials.
// No-op (returns ESP_OK) if no SSID is configured.
esp_err_t netlink_start(void);

bool netlink_is_up(void);
void netlink_get_ip(char *ip, size_t n);

#ifdef __cplusplus
}
#endif
