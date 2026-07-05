#include "netlink.h"

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
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        update_net_state(false);
        ESP_LOGW(TAG, "disconnected; retrying in 2s");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
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

esp_err_t netlink_start(void)
{
    const ns_config_t *cfg = ns_config_get();
    if (cfg->wifi.ssid[0] == '\0') {
        ESP_LOGW(TAG, "no wifi ssid configured; staying offline");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta_netif, ESP_FAIL, TAG, "sta netif");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            on_wifi, NULL, NULL), TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            on_got_ip, NULL, NULL), TAG, "ip evt");

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, cfg->wifi.ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, cfg->wifi.password, sizeof(wc.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_LOGI(TAG, "connecting to '%s'", cfg->wifi.ssid);
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
