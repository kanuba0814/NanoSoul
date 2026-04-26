#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WIFI_MAX_APS 12
#define APP_WIFI_SSID_MAX_LEN 32
#define APP_WIFI_PASSWORD_MAX_LEN 64

typedef enum {
    APP_WIFI_STATE_OFF = 0,
    APP_WIFI_STATE_READY,
    APP_WIFI_STATE_SCANNING,
    APP_WIFI_STATE_CONNECTING,
    APP_WIFI_STATE_CONNECTED,
    APP_WIFI_STATE_PINGING,
    APP_WIFI_STATE_ONLINE,
    APP_WIFI_STATE_ERROR,
} app_wifi_state_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX_LEN + 1];
    char auth[16];
    int8_t rssi;
    uint8_t channel;
} app_wifi_ap_t;

typedef struct {
    bool initialized;
    bool has_saved;
    bool connected;
    bool ping_running;
    app_wifi_state_t state;
    esp_err_t last_error;
    char ssid[APP_WIFI_SSID_MAX_LEN + 1];
    char ip[16];
    char message[96];
    uint32_t ping_sent;
    uint32_t ping_received;
    uint32_t ping_time_ms;
} app_wifi_status_t;

esp_err_t app_wifi_init(void);
esp_err_t app_wifi_scan_async(void);
esp_err_t app_wifi_connect_async(const char *ssid, const char *password, bool save);
esp_err_t app_wifi_ping_apple_async(void);
esp_err_t app_wifi_get_status(app_wifi_status_t *out);
size_t app_wifi_get_scan_results(app_wifi_ap_t *out, size_t max);

#ifdef __cplusplus
}
#endif
