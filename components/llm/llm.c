#include "llm.h"

#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "ns_config.h"
#include "telemetry.h"

static const char *TAG = "llm";

#define LLM_RESP_CAP   8192
#define LLM_TIMEOUT_MS 20000

typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        resp_t *r = (resp_t *)e->user_data;
        if (r && r->buf && r->len + e->data_len < r->cap) {
            memcpy(r->buf + r->len, e->data, e->data_len);
            r->len += e->data_len;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static char *build_body(const ns_chat_cfg_t *c, const char *user_msg, bool anthropic)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", c->model);
    cJSON_AddNumberToObject(root, "max_tokens", c->max_tokens);
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");

    if (anthropic) {
        if (c->system_prompt[0]) {
            cJSON_AddStringToObject(root, "system", c->system_prompt);
        }
    } else if (c->system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", c->system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", user_msg);
    cJSON_AddItemToArray(msgs, um);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

// Extract the reply text from a provider response. Returns true on success.
static bool parse_reply(const char *body, bool anthropic, char *out, size_t cap)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return false;
    }
    bool ok = false;

    if (anthropic) {
        cJSON *stop = cJSON_GetObjectItemCaseSensitive(root, "stop_reason");
        if (cJSON_IsString(stop) && strcmp(stop->valuestring, "refusal") == 0) {
            strlcpy(out, "(declined)", cap);
            cJSON_Delete(root);
            return false;
        }
        cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *item;
        cJSON_ArrayForEach(item, content) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
            cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
                cJSON_IsString(text)) {
                strlcpy(out, text->valuestring, cap);
                ok = true;
                break;
            }
        }
    } else {
        cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *msg = first ? cJSON_GetObjectItemCaseSensitive(first, "message") : NULL;
        cJSON *content = msg ? cJSON_GetObjectItemCaseSensitive(msg, "content") : NULL;
        if (cJSON_IsString(content)) {
            strlcpy(out, content->valuestring, cap);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

esp_err_t llm_chat(const char *user_msg, char *reply, size_t reply_cap)
{
    if (!user_msg || !reply || reply_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const ns_chat_cfg_t *c = &ns_config_get()->chat;
    if (c->api_key[0] == '\0' || c->base_url[0] == '\0') {
        strlcpy(reply, "(no cloud key configured)", reply_cap);
        return ESP_ERR_INVALID_STATE;
    }
    bool anthropic = (strcmp(c->provider, "anthropic") == 0);

    char url[192];
    snprintf(url, sizeof(url), "%s%s", c->base_url,
             anthropic ? "/v1/messages" : "/v1/chat/completions");

    char *body = build_body(c, user_msg, anthropic);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    resp_t resp = { .buf = malloc(LLM_RESP_CAP), .len = 0, .cap = LLM_RESP_CAP };
    if (!resp.buf) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    resp.buf[0] = '\0';

    telemetry_set_llm("busy");

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_evt,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = LLM_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    char auth[160];
    if (anthropic) {
        esp_http_client_set_header(cli, "x-api-key", c->api_key);
        esp_http_client_set_header(cli, "anthropic-version", "2023-06-01");
    } else {
        snprintf(auth, sizeof(auth), "Bearer %s", c->api_key);
        esp_http_client_set_header(cli, "Authorization", auth);
    }
    esp_http_client_set_post_field(cli, body, strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body);

    esp_err_t ret = ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http: %s", esp_err_to_name(err));
        strlcpy(reply, "(network error)", reply_cap);
        ret = err;
    } else if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "http status %d: %.200s", status, resp.buf);
        strlcpy(reply, "(cloud error)", reply_cap);
        ret = ESP_FAIL;
    } else if (!parse_reply(resp.buf, anthropic, reply, reply_cap)) {
        ESP_LOGW(TAG, "no reply text parsed");
        if (reply[0] == '\0') {
            strlcpy(reply, "(no reply)", reply_cap);
        }
        ret = ESP_FAIL;
    }

    free(resp.buf);
    telemetry_set_llm("idle");
    return ret;
}
