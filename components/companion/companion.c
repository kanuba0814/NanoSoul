#include "companion.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera.h"
#include "dialog.h"
#include "face.h"
#include "hud.h"
#include "motion.h"
#include "ns_config.h"
#include "soul.h"
#include "telemetry.h"

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

static void ws_send_text_all(const char *json)
{
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

/* -------- state heartbeat -------- */
static char *build_state_json(void)
{
    tel_snapshot_t t;
    telemetry_get(&t);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "v", 1);
    cJSON_AddStringToObject(r, "type", "state");
    cJSON_AddNumberToObject(r, "ts", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(r, "soul", soul_state_name(t.soul));
    cJSON_AddStringToObject(r, "emotion", t.emotion);

    cJSON *face = cJSON_AddObjectToObject(r, "face");
    cJSON_AddBoolToObject(face, "present", t.face.present);
    cJSON_AddNumberToObject(face, "cx", t.face.cx);
    cJSON_AddNumberToObject(face, "cy", t.face.cy);
    cJSON_AddNumberToObject(face, "area", t.face.area_ratio);
    cJSON_AddNumberToObject(face, "frontal", t.face.frontal_score);

    cJSON *mo = cJSON_AddObjectToObject(r, "motion");
    cJSON *intent = cJSON_AddArrayToObject(mo, "intent");
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.vx));
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.vy));
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.wz));
    cJSON *duty = cJSON_AddArrayToObject(mo, "duty");
    for (int i = 0; i < 3; i++) {
        cJSON_AddItemToArray(duty, cJSON_CreateNumber(t.motion.duty[i]));
    }
    cJSON_AddBoolToObject(mo, "enabled", t.motion.enabled);

    cJSON *enc = cJSON_AddArrayToObject(r, "enc_rpm");
    for (int i = 0; i < 3; i++) {
        cJSON_AddItemToArray(enc, cJSON_CreateNumber((int)t.enc.rpm[i]));
    }
    if (t.current_present) {
        cJSON_AddNumberToObject(r, "current_ma", (int)(t.current_a * 1000));
    } else {
        cJSON_AddNullToObject(r, "current_ma");
    }
    cJSON *net = cJSON_AddObjectToObject(r, "net");
    cJSON_AddStringToObject(net, "ip", t.net_up ? t.ip : "");
    cJSON_AddNumberToObject(net, "rssi", t.rssi);
    cJSON_AddStringToObject(r, "llm", t.llm);
    cJSON_AddStringToObject(r, "voice", t.voice);
    cJSON_AddNumberToObject(r, "heap", t.free_heap);
    cJSON *fps = cJSON_AddObjectToObject(r, "fps");
    cJSON_AddNumberToObject(fps, "render", t.fps_render);
    cJSON_AddNumberToObject(fps, "detect", t.fps_detect);

    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}

static void push_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_server) {
            continue;
        }
        char *s = build_state_json();
        if (s) {
            ws_send_text_all(s);
            free(s);
        }
    }
}

/* -------- telemetry event bus -> event frames -------- */
static void emit_event(const char *name, cJSON *data /*takes ownership*/)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "v", 1);
    cJSON_AddStringToObject(r, "type", "event");
    cJSON_AddNumberToObject(r, "ts", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(r, "name", name);
    cJSON_AddItemToObject(r, "data", data ? data : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (s) {
        ws_send_text_all(s);
        free(s);
    }
}

static void on_ns_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch ((ns_event_id_t)id) {
    case NS_EVT_SOUL_TRANSITION: {
        ns_evt_soul_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "from", soul_state_name(e->from));
        cJSON_AddStringToObject(d, "to", soul_state_name(e->to));
        emit_event("soul_transition", d);
        break;
    }
    case NS_EVT_FACE_PRESENT: emit_event("face_present", NULL); break;
    case NS_EVT_FACE_LOST:    emit_event("face_lost", NULL); break;
    case NS_EVT_GAZED:        emit_event("gazed", NULL); break;
    case NS_EVT_WAKE:         emit_event("wake", NULL); break;
    case NS_EVT_LLM_REPLY: {
        ns_evt_text_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", e->text);
        emit_event("llm_reply", d);
        break;
    }
    case NS_EVT_FAULT: {
        ns_evt_text_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", e->text);
        emit_event("fault", d);
        break;
    }
    default: break;
    }
}

/* -------- command handling -------- */
static void send_ack(httpd_req_t *req, int id, bool ok, const char *err)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "type", "ack");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddBoolToObject(r, "ok", ok);
    if (err) {
        cJSON_AddStringToObject(r, "err", err);
    }
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (s) {
        httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)s, .len = strlen(s) };
        httpd_ws_send_frame(req, &f);
        free(s);
    }
}

static void send_snapshot(httpd_req_t *req)
{
    uint8_t *jpg = NULL;
    size_t len = 0;
    if (camera_snapshot_jpeg(&jpg, &len) == ESP_OK && jpg) {
        httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_BINARY, .payload = jpg, .len = len };
        httpd_ws_send_frame(req, &f);
        free(jpg);
    }
}

static void handle_command(httpd_req_t *req, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *jid = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(jid) ? jid->valueint : 0;
    const char *cmd = cJSON_IsString(type) ? type->valuestring : "";

    if (strcmp(cmd, "ask") == 0) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "text");
        bool ok = cJSON_IsString(t) && dialog_ask(t->valuestring) == ESP_OK;
        send_ack(req, id, ok, ok ? NULL : "ask failed");
    } else if (strcmp(cmd, "set_emotion") == 0) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "name");
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mode");
        if (cJSON_IsString(n)) {
            bool fade = cJSON_IsString(m) && strcmp(m->valuestring, "fade") == 0;
            face_set_emotion(n->valuestring, fade ? FACE_FADE : FACE_NOW);
            soul_emotion_override(n->valuestring, 10000);
            send_ack(req, id, true, NULL);
        } else {
            send_ack(req, id, false, "no name");
        }
    } else if (strcmp(cmd, "teleop") == 0) {
        if (!motion_enabled()) {
            send_ack(req, id, false, "motion disabled");
        } else {
            const cJSON *vx = cJSON_GetObjectItemCaseSensitive(root, "vx");
            const cJSON *vy = cJSON_GetObjectItemCaseSensitive(root, "vy");
            const cJSON *wz = cJSON_GetObjectItemCaseSensitive(root, "wz");
            motion_set_intent(cJSON_IsNumber(vx) ? vx->valuedouble : 0,
                              cJSON_IsNumber(vy) ? vy->valuedouble : 0,
                              cJSON_IsNumber(wz) ? wz->valuedouble : 0);
            send_ack(req, id, true, NULL);
        }
    } else if (strcmp(cmd, "get_snapshot") == 0) {
        send_snapshot(req);
        send_ack(req, id, true, NULL);
    } else if (strcmp(cmd, "estop") == 0) {
        soul_notify_fault("estop (companion)");
        send_ack(req, id, true, NULL);
    } else if (strcmp(cmd, "clear_fault") == 0) {
        soul_clear_fault();
        send_ack(req, id, true, NULL);
    } else if (strcmp(cmd, "config_reload") == 0) {
        ns_config_init(NS_CONFIG_PATH);
        send_ack(req, id, true, NULL);
    } else if (strcmp(cmd, "set_overlay") == 0) {
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
        hud_set_enabled(cJSON_IsBool(on) ? cJSON_IsTrue(on) : true);
        send_ack(req, id, true, NULL);
    } else if (strcmp(cmd, "selftest") == 0) {
        send_ack(req, id, true, "see serial");
    } else {
        send_ack(req, id, false, "unknown cmd");
    }
    cJSON_Delete(root);
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
            handle_command(req, (char *)f.payload);
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

    esp_event_handler_instance_register(NANOSOUL_EVENT, ESP_EVENT_ANY_ID, on_ns_event, NULL, NULL);
    xTaskCreatePinnedToCore(push_task, "companion", 6144, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "WS server up on :80/ws");
    return ESP_OK;
}

bool companion_running(void)
{
    return s_server != NULL;
}
