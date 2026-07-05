#include "llm.h"

#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
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

/* ------------------------- STT / TTS (OpenAI-compatible) ------------------------- */

static void wav_header(uint8_t h[44], int rate, uint32_t data_len)
{
    const int ch = 1, bits = 16;
    uint32_t byte_rate = (uint32_t)rate * ch * bits / 8;
    uint16_t block_align = ch * bits / 8;
    uint32_t chunk = 36 + data_len;
    memcpy(h, "RIFF", 4);          memcpy(h + 4, &chunk, 4);       memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    uint32_t fmt_len = 16; uint16_t pcm = 1, chan = ch, bps = bits;
    memcpy(h + 16, &fmt_len, 4);   memcpy(h + 20, &pcm, 2);        memcpy(h + 22, &chan, 2);
    memcpy(h + 24, &rate, 4);      memcpy(h + 28, &byte_rate, 4);  memcpy(h + 32, &block_align, 2);
    memcpy(h + 34, &bps, 2);       memcpy(h + 36, "data", 4);      memcpy(h + 40, &data_len, 4);
}

esp_err_t llm_stt(const int16_t *pcm, size_t samples, int sample_rate, char *text, size_t text_cap)
{
    const ns_stt_cfg_t *c = &ns_config_get()->stt;
    if (c->base_url[0] == '\0' || c->api_key[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    const char *B = "----NanoSoulSTTBoundary";
    uint32_t data_len = samples * sizeof(int16_t);

    char pre[256], post[64];
    int pre_n = snprintf(pre, sizeof(pre),
                         "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
                         "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\n"
                         "Content-Type: audio/wav\r\n\r\n", B, c->model, B);
    int post_n = snprintf(post, sizeof(post), "\r\n--%s--\r\n", B);

    size_t body_len = pre_n + 44 + data_len + post_n;
    uint8_t *body = heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    size_t off = 0;
    memcpy(body + off, pre, pre_n);           off += pre_n;
    wav_header(body + off, sample_rate, data_len); off += 44;
    memcpy(body + off, pcm, data_len);        off += data_len;
    memcpy(body + off, post, post_n);         off += post_n;

    char url[192];
    snprintf(url, sizeof(url), "%s/v1/audio/transcriptions", c->base_url);
    resp_t resp = { .buf = malloc(2048), .len = 0, .cap = 2048 };
    if (!resp.buf) { free(body); return ESP_ERR_NO_MEM; }
    resp.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .event_handler = http_evt,
        .user_data = &resp, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = LLM_TIMEOUT_MS, .buffer_size = 2048,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    char ctype[80], auth[160];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", B);
    snprintf(auth, sizeof(auth), "Bearer %s", c->api_key);
    esp_http_client_set_header(cli, "Content-Type", ctype);
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_post_field(cli, (const char *)body, body_len);

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body);

    esp_err_t ret = ESP_FAIL;
    if (err == ESP_OK && status >= 200 && status < 300) {
        cJSON *root = cJSON_Parse(resp.buf);
        cJSON *t = root ? cJSON_GetObjectItemCaseSensitive(root, "text") : NULL;
        if (cJSON_IsString(t)) {
            strlcpy(text, t->valuestring, text_cap);
            ret = ESP_OK;
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "stt http err=%s status=%d", esp_err_to_name(err), status);
    }
    free(resp.buf);
    return ret;
}

esp_err_t llm_tts(const char *text, int out_rate, int16_t **pcm_out, size_t *samples_out)
{
    const ns_tts_cfg_t *c = &ns_config_get()->tts;
    if (c->base_url[0] == '\0' || c->api_key[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", c->model);
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", c->voice);
    cJSON_AddStringToObject(root, "response_format", "wav");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    /* WAV can be a few hundred KB — accumulate into PSRAM. */
    resp_t resp = { .buf = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM), .len = 0, .cap = 1024 * 1024 };
    if (!resp.buf || !body) { free(body); free(resp.buf); return ESP_ERR_NO_MEM; }

    char url[192], auth[160];
    snprintf(url, sizeof(url), "%s/v1/audio/speech", c->base_url);
    snprintf(auth, sizeof(auth), "Bearer %s", c->api_key);
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .event_handler = http_evt,
        .user_data = &resp, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = LLM_TIMEOUT_MS, .buffer_size = 4096,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body);

    if (err != ESP_OK || status < 200 || status >= 300 || resp.len < 44) {
        ESP_LOGE(TAG, "tts http err=%s status=%d len=%d", esp_err_to_name(err), status, resp.len);
        free(resp.buf);
        return ESP_FAIL;
    }

    /* Parse minimal WAV: rate at offset 24, pcm after the 44-byte header. */
    int in_rate = 16000;
    memcpy(&in_rate, resp.buf + 24, 4);
    const int16_t *in = (const int16_t *)(resp.buf + 44);
    size_t in_samples = (resp.len - 44) / sizeof(int16_t);

    /* Naive nearest-neighbour resample to out_rate. */
    size_t out_samples = (size_t)((uint64_t)in_samples * out_rate / (in_rate ? in_rate : out_rate));
    int16_t *out = heap_caps_malloc(out_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!out) { free(resp.buf); return ESP_ERR_NO_MEM; }
    for (size_t i = 0; i < out_samples; i++) {
        size_t j = (size_t)((uint64_t)i * in_rate / out_rate);
        out[i] = (j < in_samples) ? in[j] : 0;
    }
    free(resp.buf);
    *pcm_out = out;
    *samples_out = out_samples;
    ESP_LOGI(TAG, "tts %uHz->%uHz %u samples", (unsigned)in_rate, (unsigned)out_rate, (unsigned)out_samples);
    return ESP_OK;
}
