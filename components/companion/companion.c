#include "companion.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera.h"
#include "ns_config.h"
#include "testlink.h"

/*
 * companion — the WS transport shell over the shared testlink protocol core.
 *
 * Owns only: the esp_http_server WS endpoint (/ws), token auth, client fd
 * bookkeeping, the text-frame sink (async broadcast to clients), and the JPEG
 * snapshot (WS-binary only). Command dispatch, the 1 Hz state heartbeat and the
 * telemetry→event forwarding all live in testlink now, so the serial link (T4)
 * shares them over the same core.
 */

static const char *TAG = "companion";

#define MAX_CLIENTS 3

static httpd_handle_t s_server;
static int            s_clients[MAX_CLIENTS];
static const char    *s_token;

/* -------- client fd bookkeeping -------- */
static void client_add(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == fd) {
            return;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] < 0) {
            s_clients[i] = fd;
            ESP_LOGI(TAG, "client %d connected", fd);
            return;
        }
    }
}

static void client_remove(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == fd) {
            s_clients[i] = -1;
        }
    }
}

/* text-frame sink: async broadcast of state/event/sense frames to all clients */
static void ws_send_text_all(const char *json)
{
    if (!s_server) {
        return;
    }
    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] >= 0) {
            if (httpd_ws_send_frame_async(s_server, s_clients[i], &f) != ESP_OK) {
                client_remove(s_clients[i]);
            }
        }
    }
}

/* -------- reply + snapshot hooks handed to the protocol core -------- */
/* ack/reply is request-scoped (synchronous frame on the calling handler's req). */
static void ws_reply(void *ctx, const char *json)
{
    httpd_req_t *req = ctx;
    httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = strlen(json) };
    httpd_ws_send_frame(req, &f);
}

static bool ws_snapshot(void *ctx)
{
    httpd_req_t *req = ctx;
    uint8_t *jpg = NULL;
    size_t len = 0;
    if (camera_snapshot_jpeg(&jpg, &len) == ESP_OK && jpg) {
        httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_BINARY, .payload = jpg, .len = len };
        httpd_ws_send_frame(req, &f);
        free(jpg);
        return true;
    }
    return false;
}

/* -------- WS URI handler -------- */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* handshake — check token */
        char *q = NULL;
        size_t qlen = httpd_req_get_url_query_len(req) + 1;
        bool ok = (s_token == NULL || s_token[0] == '\0');
        if (!ok && qlen > 1) {
            q = malloc(qlen);
            if (q && httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
                char tok[64] = {0};
                if (httpd_query_key_value(q, "token", tok, sizeof(tok)) == ESP_OK) {
                    ok = strcmp(tok, s_token) == 0;
                }
            }
            free(q);
        }
        if (!ok) {
            ESP_LOGW(TAG, "rejected: bad token");
            return ESP_FAIL;
        }
        client_add(httpd_req_to_sockfd(req));
        /* push a hwinfo frame to the freshly-connected client */
        char *hw = ns_build_hwinfo_json("");
        if (hw) {
            httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)hw, .len = strlen(hw) };
            httpd_ws_send_frame(req, &f);
            free(hw);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t f = {0};
    f.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &f, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (f.len && f.len < 2048) {
        f.payload = malloc(f.len + 1);
        if (f.payload && httpd_ws_recv_frame(req, &f, f.len) == ESP_OK) {
            f.payload[f.len] = 0;
            ns_proto_handle((char *)f.payload, f.len, ws_reply, req);
        }
        free(f.payload);
    }
    return ESP_OK;
}

esp_err_t companion_start(void)
{
    const ns_config_t *cfg = ns_config_get();
    if (!cfg->companion.enabled) {
        ESP_LOGI(TAG, "companion disabled in config");
        return ESP_OK;
    }
    s_token = cfg->companion.token;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        s_clients[i] = -1;
    }

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.server_port = 80;
    hc.max_open_sockets = MAX_CLIENTS + 1;
    if (httpd_start(&s_server, &hc) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return ESP_FAIL;
    }
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws);

    /* Wire this transport into the shared protocol core. */
    ns_proto_set_snapshot_hook(ws_snapshot);
    ns_link_add_sink(ws_send_text_all);
    testlink_core_start();   /* idempotent: state push + event forwarding */

    ESP_LOGI(TAG, "WS server up on :80/ws");
    return ESP_OK;
}

bool companion_running(void)
{
    return s_server != NULL;
}
