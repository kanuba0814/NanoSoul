#include "net_wifi.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"

static const char *TAG = "app_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_NAMESPACE     "wifi_cfg"
#define WIFI_NVS_SSID      "ssid"
#define WIFI_NVS_PASS      "pass"
#define WIFI_CONNECT_RETRY 3
#define WIFI_CONNECT_WAIT_MS 20000
#define APPLE_TEST_HOST    "captive.apple.com"

typedef struct {
    char ssid[APP_WIFI_SSID_MAX_LEN + 1];
    char password[APP_WIFI_PASSWORD_MAX_LEN + 1];
    bool save;
} connect_args_t;

typedef struct {
    SemaphoreHandle_t done;
    uint32_t received;
    uint32_t transmitted;
    uint32_t elapsed_ms;
} ping_ctx_t;

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_event_group;
static esp_netif_t *s_sta_netif;
static app_wifi_status_t s_status = {
    .state = APP_WIFI_STATE_OFF,
    .message = "Wi-Fi not initialized",
};
static app_wifi_ap_t s_aps[APP_WIFI_MAX_APS];
static size_t s_ap_count;
static bool s_connecting;
static bool s_scanning;
static bool s_reconfiguring;
static int s_retry_num;

static void lock_status(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_status(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void set_message_locked(esp_err_t err, const char *message)
{
    s_status.last_error = err;
    strlcpy(s_status.message, message, sizeof(s_status.message));
}

static void set_error_locked(esp_err_t err, const char *message)
{
    s_status.state = APP_WIFI_STATE_ERROR;
    s_status.last_error = err;
    strlcpy(s_status.message, message, sizeof(s_status.message));
}

static const char *auth_name(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
    case WIFI_AUTH_OWE: return "OWE";
    default: return "secure";
    }
}

static int ap_rssi_desc(const void *a, const void *b)
{
    const app_wifi_ap_t *aa = (const app_wifi_ap_t *)a;
    const app_wifi_ap_t *bb = (const app_wifi_ap_t *)b;
    return (int)bb->rssi - (int)aa->rssi;
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs");
        ret = nvs_flash_init();
    }
    return ret;
}

static bool load_saved_credentials(char *ssid, size_t ssid_len,
                                   char *password, size_t password_len)
{
    nvs_handle_t nvs = 0;
    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    size_t len = ssid_len;
    esp_err_t err = nvs_get_str(nvs, WIFI_NVS_SSID, ssid, &len);
    if (err != ESP_OK || ssid[0] == '\0') {
        nvs_close(nvs);
        return false;
    }

    len = password_len;
    err = nvs_get_str(nvs, WIFI_NVS_PASS, password, &len);
    if (err != ESP_OK) {
        password[0] = '\0';
    }

    nvs_close(nvs);
    return true;
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs = 0;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs), TAG, "open nvs");
    esp_err_t err = nvs_set_str(nvs, WIFI_NVS_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_NVS_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        lock_status();
        if (s_connecting) {
            set_message_locked(ESP_OK, "Associated with access point");
        }
        unlock_status();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        lock_status();
        bool reconfiguring = s_reconfiguring;
        bool should_retry = s_connecting && s_retry_num < WIFI_CONNECT_RETRY;
        if (!reconfiguring) {
            s_status.connected = false;
            s_status.ping_running = false;
            s_status.ip[0] = '\0';
            s_status.ping_sent = 0;
            s_status.ping_received = 0;
            s_status.ping_time_ms = 0;
        }
        if (reconfiguring) {
            unlock_status();
            return;
        }
        if (should_retry) {
            s_retry_num++;
            s_status.state = APP_WIFI_STATE_CONNECTING;
            snprintf(s_status.message, sizeof(s_status.message),
                     "Retrying Wi-Fi (%d/%d)", s_retry_num, WIFI_CONNECT_RETRY);
            unlock_status();
            esp_wifi_connect();
            return;
        }
        if (s_connecting) {
            s_connecting = false;
            s_status.state = APP_WIFI_STATE_ERROR;
            s_status.last_error = ESP_FAIL;
            snprintf(s_status.message, sizeof(s_status.message),
                     "Connect failed, reason %" PRIi32, (int32_t)event->reason);
            xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        } else {
            s_status.state = APP_WIFI_STATE_READY;
            snprintf(s_status.message, sizeof(s_status.message),
                     "Disconnected, reason %" PRIi32, (int32_t)event->reason);
        }
        unlock_status();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        lock_status();
        s_status.connected = true;
        s_status.state = APP_WIFI_STATE_CONNECTED;
        s_status.last_error = ESP_OK;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(s_status.message, sizeof(s_status.message), "Connected, IP %s", s_status.ip);
        s_retry_num = 0;
        s_connecting = false;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        unlock_status();
    }
}

static void scan_task(void *arg)
{
    (void)arg;

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK) {
        uint16_t ap_num = 0;
        uint16_t number = APP_WIFI_MAX_APS;
        wifi_ap_record_t records[APP_WIFI_MAX_APS] = {0};
        err = esp_wifi_scan_get_ap_num(&ap_num);
        if (err == ESP_OK) {
            err = esp_wifi_scan_get_ap_records(&number, records);
        }

        lock_status();
        if (err == ESP_OK) {
            s_ap_count = number;
            for (size_t i = 0; i < s_ap_count; ++i) {
                strlcpy(s_aps[i].ssid, (const char *)records[i].ssid, sizeof(s_aps[i].ssid));
                strlcpy(s_aps[i].auth, auth_name(records[i].authmode), sizeof(s_aps[i].auth));
                s_aps[i].rssi = records[i].rssi;
                s_aps[i].channel = records[i].primary;
            }
            qsort(s_aps, s_ap_count, sizeof(s_aps[0]), ap_rssi_desc);
            s_status.state = s_status.connected ? APP_WIFI_STATE_CONNECTED : APP_WIFI_STATE_READY;
            s_status.last_error = ESP_OK;
            snprintf(s_status.message, sizeof(s_status.message),
                     "Scan found %u AP%s", ap_num, ap_num == 1 ? "" : "s");
        } else {
            set_error_locked(err, "Wi-Fi scan failed");
        }
        s_scanning = false;
        unlock_status();
    } else {
        lock_status();
        s_scanning = false;
        set_error_locked(err, "Wi-Fi scan failed");
        unlock_status();
    }

    vTaskDelete(NULL);
}

static void connect_task(void *arg)
{
    connect_args_t *args = (connect_args_t *)arg;

    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    lock_status();
    s_reconfiguring = true;
    s_connecting = false;
    s_status.connected = false;
    s_status.ping_running = false;
    s_status.state = APP_WIFI_STATE_CONNECTING;
    s_status.last_error = ESP_OK;
    s_status.ip[0] = '\0';
    s_status.ping_sent = 0;
    s_status.ping_received = 0;
    s_status.ping_time_ms = 0;
    strlcpy(s_status.ssid, args->ssid, sizeof(s_status.ssid));
    snprintf(s_status.message, sizeof(s_status.message), "Connecting to %s", args->ssid);
    unlock_status();

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, args->ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, args->password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = args->password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }

    lock_status();
    s_reconfiguring = false;
    s_retry_num = 0;
    s_connecting = true;
    unlock_status();

    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }

    if (err != ESP_OK) {
        lock_status();
        s_connecting = false;
        set_error_locked(err, "Wi-Fi connect start failed");
        unlock_status();
        free(args);
        vTaskDelete(NULL);
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_WAIT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        if (args->save) {
            esp_err_t save_err = save_credentials(args->ssid, args->password);
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "save credentials failed: %s", esp_err_to_name(save_err));
            } else {
                lock_status();
                s_status.has_saved = true;
                unlock_status();
            }
        }
        app_wifi_ping_apple_async();
    } else if (!(bits & WIFI_FAIL_BIT)) {
        lock_status();
        s_connecting = false;
        set_error_locked(ESP_ERR_TIMEOUT, "Wi-Fi connect timed out");
        unlock_status();
        esp_wifi_disconnect();
    }

    free(args);
    vTaskDelete(NULL);
}

static void ping_success_cb(esp_ping_handle_t hdl, void *args)
{
    ping_ctx_t *ctx = (ping_ctx_t *)args;
    uint32_t elapsed_time = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    ctx->received++;
    ctx->elapsed_ms = elapsed_time;
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args)
{
    ping_ctx_t *ctx = (ping_ctx_t *)args;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t duration = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &duration, sizeof(duration));
    ctx->transmitted = transmitted;
    ctx->received = received;
    ctx->elapsed_ms = duration;
    xSemaphoreGive(ctx->done);
}

static void ping_task(void *arg)
{
    (void)arg;

    ip_addr_t target_addr = {0};
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_RAW,
    };
    struct addrinfo *res = NULL;
    int gai = getaddrinfo(APPLE_TEST_HOST, NULL, &hint, &res);
    if (gai != 0 || !res) {
        lock_status();
        s_status.ping_running = false;
        s_status.state = APP_WIFI_STATE_ERROR;
        s_status.last_error = ESP_FAIL;
        strlcpy(s_status.message, "DNS lookup failed", sizeof(s_status.message));
        unlock_status();
        vTaskDelete(NULL);
        return;
    }

    struct in_addr addr4 = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);

    ping_ctx_t ctx = {
        .done = xSemaphoreCreateBinary(),
    };
    if (!ctx.done) {
        lock_status();
        s_status.ping_running = false;
        set_error_locked(ESP_ERR_NO_MEM, "Ping semaphore failed");
        unlock_status();
        vTaskDelete(NULL);
        return;
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = 4;
    ping_config.interval_ms = 500;
    ping_config.timeout_ms = 1000;

    esp_ping_callbacks_t cbs = {
        .cb_args = &ctx,
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = NULL,
        .on_ping_end = ping_end_cb,
    };
    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (err == ESP_OK) {
        err = esp_ping_start(ping);
    }

    if (err == ESP_OK &&
        xSemaphoreTake(ctx.done, pdMS_TO_TICKS(10000)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
        esp_ping_stop(ping);
    }

    if (ping) {
        esp_ping_delete_session(ping);
    }
    vSemaphoreDelete(ctx.done);

    lock_status();
    s_status.ping_running = false;
    s_status.ping_sent = ctx.transmitted;
    s_status.ping_received = ctx.received;
    s_status.ping_time_ms = ctx.elapsed_ms;
    if (err == ESP_OK && ctx.received > 0) {
        s_status.state = APP_WIFI_STATE_ONLINE;
        s_status.last_error = ESP_OK;
        snprintf(s_status.message, sizeof(s_status.message),
                 "Internet OK: %" PRIu32 "/%" PRIu32 " replies",
                 ctx.received, ctx.transmitted);
    } else if (err == ESP_ERR_TIMEOUT) {
        set_error_locked(err, "Apple ping timed out");
    } else {
        set_error_locked(err, "Apple ping failed");
    }
    unlock_status();

    vTaskDelete(NULL);
}

esp_err_t app_wifi_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    lock_status();
    if (s_status.initialized) {
        unlock_status();
        return ESP_OK;
    }
    unlock_status();

    esp_err_t err = init_nvs();
    if (err != ESP_OK) {
        lock_status();
        set_error_locked(err, "NVS init failed");
        unlock_status();
        return err;
    }

    s_event_group = xEventGroupCreate();
    if (!s_event_group) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        lock_status();
        set_error_locked(err, "esp_netif init failed");
        unlock_status();
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        lock_status();
        set_error_locked(err, "event loop init failed");
        unlock_status();
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        lock_status();
        set_error_locked(ESP_FAIL, "STA netif create failed");
        unlock_status();
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;

    char saved_ssid[APP_WIFI_SSID_MAX_LEN + 1] = {0};
    char saved_pass[APP_WIFI_PASSWORD_MAX_LEN + 1] = {0};
    bool has_saved = load_saved_credentials(saved_ssid, sizeof(saved_ssid),
                                            saved_pass, sizeof(saved_pass));

    lock_status();
    s_status.initialized = true;
    s_status.has_saved = has_saved;
    s_status.state = APP_WIFI_STATE_READY;
    s_status.last_error = ESP_OK;
    strlcpy(s_status.message, has_saved ? "Loaded saved Wi-Fi" : "Wi-Fi ready",
            sizeof(s_status.message));
    unlock_status();

    if (has_saved) {
        app_wifi_connect_async(saved_ssid, saved_pass, false);
    }

    return ESP_OK;

fail:
    lock_status();
    set_error_locked(err, "Wi-Fi init failed");
    unlock_status();
    return err;
}

esp_err_t app_wifi_scan_async(void)
{
    lock_status();
    if (!s_status.initialized) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scanning || s_connecting || s_status.ping_running) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }
    s_scanning = true;
    s_status.state = APP_WIFI_STATE_SCANNING;
    s_status.last_error = ESP_OK;
    strlcpy(s_status.message, "Scanning Wi-Fi networks", sizeof(s_status.message));
    unlock_status();

    BaseType_t ok = xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 4, NULL);
    if (ok != pdPASS) {
        lock_status();
        s_scanning = false;
        set_error_locked(ESP_ERR_NO_MEM, "Scan task create failed");
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_wifi_connect_async(const char *ssid, const char *password, bool save)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > APP_WIFI_SSID_MAX_LEN) {
        lock_status();
        if (s_status.initialized) {
            set_error_locked(ESP_ERR_INVALID_ARG, "SSID required");
        }
        unlock_status();
        return ESP_ERR_INVALID_ARG;
    }
    if (password && strlen(password) > APP_WIFI_PASSWORD_MAX_LEN) {
        lock_status();
        if (s_status.initialized) {
            set_error_locked(ESP_ERR_INVALID_ARG, "Password too long");
        }
        unlock_status();
        return ESP_ERR_INVALID_ARG;
    }

    lock_status();
    if (!s_status.initialized || s_connecting || s_scanning || s_status.ping_running) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_status();

    connect_args_t *args = calloc(1, sizeof(*args));
    if (!args) {
        lock_status();
        set_error_locked(ESP_ERR_NO_MEM, "Connect task create failed");
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    strlcpy(args->ssid, ssid, sizeof(args->ssid));
    strlcpy(args->password, password ? password : "", sizeof(args->password));
    args->save = save;

    BaseType_t ok = xTaskCreate(connect_task, "wifi_connect", 4096, args, 4, NULL);
    if (ok != pdPASS) {
        free(args);
        lock_status();
        set_error_locked(ESP_ERR_NO_MEM, "Connect task create failed");
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_wifi_ping_apple_async(void)
{
    lock_status();
    if (!s_status.initialized || !s_status.connected || s_status.ping_running) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }
    s_status.ping_running = true;
    s_status.state = APP_WIFI_STATE_PINGING;
    s_status.last_error = ESP_OK;
    s_status.ping_sent = 0;
    s_status.ping_received = 0;
    s_status.ping_time_ms = 0;
    strlcpy(s_status.message, "Pinging captive.apple.com", sizeof(s_status.message));
    unlock_status();

    BaseType_t ok = xTaskCreate(ping_task, "wifi_ping", 4096, NULL, 4, NULL);
    if (ok != pdPASS) {
        lock_status();
        s_status.ping_running = false;
        set_error_locked(ESP_ERR_NO_MEM, "Ping task create failed");
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_wifi_get_status(app_wifi_status_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_status();
    *out = s_status;
    unlock_status();
    return ESP_OK;
}

size_t app_wifi_get_scan_results(app_wifi_ap_t *out, size_t max)
{
    if (!out || max == 0) {
        return 0;
    }
    lock_status();
    size_t n = s_ap_count < max ? s_ap_count : max;
    memcpy(out, s_aps, n * sizeof(out[0]));
    unlock_status();
    return n;
}
