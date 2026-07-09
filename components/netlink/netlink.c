#include "netlink.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

#include "ns_config.h"
#include "telemetry.h"

static const char *TAG = "netlink";

static esp_netif_t *s_sta_netif;
static volatile bool s_up;
static char          s_ip[16];
static bool          s_sta_started;   // esp_wifi STA running (scan-capable)
static bool          s_have_creds;    // a target AP is set -> auto-reconnect

static void update_net_state(bool up)
{
    s_up = up;
    int8_t rssi = 0;
    if (up) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }
    } else {
        s_ip[0] = '\0';
    }
    telemetry_set_net(up, s_ip, rssi);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_creds) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        update_net_state(false);
        if (s_have_creds) {
            ESP_LOGW(TAG, "disconnected; retrying in 2s");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        }
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
    ESP_LOGI(TAG, "online: %s", s_ip);
    update_net_state(true);

    // SNTP (once); mDNS for nanosoul.local
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("nanosoul");
        mdns_instance_name_set("NanoSoul");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
}

// Idempotent: bring the Wi-Fi STA up (netif + driver + events + start).
static esp_err_t sta_up(void)
{
    if (s_sta_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    ESP_RETURN_ON_FALSE(s_sta_netif, ESP_FAIL, TAG, "sta netif");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            on_wifi, NULL, NULL), TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            on_got_ip, NULL, NULL), TAG, "ip evt");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    s_sta_started = true;
    ESP_LOGI(TAG, "Wi-Fi STA up");
    return ESP_OK;
}

esp_err_t netlink_start(void)
{
    ESP_RETURN_ON_ERROR(sta_up(), TAG, "sta up");
    const ns_config_t *cfg = ns_config_get();
    if (cfg->wifi.ssid[0]) {
        return netlink_connect(cfg->wifi.ssid, cfg->wifi.password);
    }
    ESP_LOGI(TAG, "no SD ssid — STA idle, scan/connect from the on-screen menu");
    return ESP_OK;
}

esp_err_t netlink_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "no ssid");
    ESP_RETURN_ON_ERROR(sta_up(), TAG, "sta up");

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    if (password) {
        strlcpy((char *)wc.sta.password, password, sizeof(wc.sta.password));
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "config");
    s_have_creds = true;
    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    esp_wifi_disconnect();
    esp_wifi_connect();   // errors surface via the disconnect event/telemetry
    return ESP_OK;
}

esp_err_t netlink_scan(netlink_ap_t *out, int max, int *count)
{
    if (!out || max <= 0 || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = 0;
    ESP_RETURN_ON_ERROR(sta_up(), TAG, "sta up");

    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);   // blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
        return err;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) {
        return ESP_OK;
    }
    if (n > 24) {
        n = 24;
    }
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    if (!recs) {
        return ESP_ERR_NO_MEM;
    }
    uint16_t got = n;
    esp_wifi_scan_get_ap_records(&got, recs);
    int k = 0;
    for (int i = 0; i < got && k < max; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;   // hidden
        }
        strlcpy(out[k].ssid, (char *)recs[i].ssid, sizeof(out[k].ssid));
        out[k].rssi = recs[i].rssi;
        out[k].authmode = recs[i].authmode;
        k++;
    }
    *count = k;
    free(recs);
    ESP_LOGI(TAG, "scan: %d APs", k);
    return ESP_OK;
}

bool netlink_is_up(void)
{
    return s_up;
}

void netlink_get_ip(char *ip, size_t n)
{
    strlcpy(ip, s_ip, n);
}
