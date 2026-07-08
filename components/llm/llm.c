#include "llm.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include "ns_config.h"
#include "telemetry.h"
#include "volc_asr.h"

static const char *TAG = "llm";

#define LLM_RESP_CAP   8192
#define LLM_TIMEOUT_MS 20000
/* volc TTS (方舟 Agent Plan /api/v3/plan/tts/unidirectional) returns the audio as
 * a run of concatenated JSON objects (no reliable delimiter), each with a base64
 * PCM chunk; we buffer the whole response then harvest+decode. A bounded (<=512B)
 * reply is at most a few tens of seconds of 16k PCM -> ~1.7MB base64. Headroom. */
#define TTS_RESP_CAP   (2 * 1024 * 1024)

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
    bool anthropic = (strcmp(c->provider, "anthropic") == 0);
    bool volc = (strcmp(c->provider, "volc") == 0);
    /* Ark is OpenAI-compatible: same body/parse as "openai", only the URL path
     * differs, and base_url may be left blank (defaulted here). */
    const char *base = c->base_url[0] ? c->base_url
                                      : (volc ? "https://ark.cn-beijing.volces.com" : "");
    if (c->api_key[0] == '\0' || base[0] == '\0') {
        strlcpy(reply, "(no cloud key configured)", reply_cap);
        return ESP_ERR_INVALID_STATE;
    }

    /* Method path per provider. volc base_url may already carry the full API
     * prefix (Agent Plan chat = ".../api/plan/v3", pay-as-you-go = ".../api/v3")
     * — then append only the method; a bare host gets the classic /api/v3. */
    const char *path;
    if (anthropic) {
        path = "/v1/messages";
    } else if (volc && strstr(base, "/api/")) {
        path = "/chat/completions";
    } else if (volc) {
        path = "/api/v3/chat/completions";
    } else {
        path = "/v1/chat/completions";
    }
    char url[224];
    snprintf(url, sizeof(url), "%s%s", base, path);

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
    if (strcmp(c->provider, "volc") == 0) {
        /* 方舟 Agent Plan ASR (doubao-seed-asr-2.0): single-stream WebSocket,
         * binary framed + gzip. text[0] is guaranteed empty on any failure. */
        return volc_asr_recognize(c, pcm, samples, sample_rate, text, text_cap);
    }
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

/* 火山方舟 Agent Plan TTS — doubao-seed-tts-2.0, /api/v3/plan/tts/unidirectional.
 * One POST; the response is a run of CONCATENATED JSON objects with no reliable
 * delimiter, each {"code":n,"data":"<base64>",...}: code 0 chunks carry audio,
 * 20000000 = done. We request format=pcm@rate, so each decoded chunk is raw
 * s16le-mono PCM at out_rate — concatenate straight into audio_play's format, no
 * container, no resample. Auth is the 方舟专属 API Key via the X-Api-Key header. */
static esp_err_t tts_volc(const ns_tts_cfg_t *c, const char *text, int out_rate,
                          int16_t **pcm_out, size_t *samples_out)
{
    if (c->api_key[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *rp = cJSON_AddObjectToObject(root, "req_params");
    cJSON_AddStringToObject(rp, "text", text);
    cJSON_AddStringToObject(rp, "speaker", c->voice);
    cJSON *ap = cJSON_AddObjectToObject(rp, "audio_params");
    cJSON_AddStringToObject(ap, "format", "pcm");        /* raw s16le mono, directly playable */
    cJSON_AddNumberToObject(ap, "sample_rate", out_rate); /* 8000/16000/22050/24000/... */
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    resp_t resp = { .buf = heap_caps_malloc(TTS_RESP_CAP, MALLOC_CAP_SPIRAM),
                    .len = 0, .cap = TTS_RESP_CAP };
    if (!resp.buf) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    resp.buf[0] = '\0';

    const char *host = c->base_url[0] ? c->base_url : "https://openspeech.bytedance.com";
    char url[256];
    /* doc path carries v3; the Agent-Plan route is also served at
     * /api/plan/tts/unidirectional (no v3) if this ever 404s. */
    snprintf(url, sizeof(url), "%s/api/v3/plan/tts/unidirectional", host);

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .event_handler = http_evt,
        .user_data = &resp, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = LLM_TIMEOUT_MS, .buffer_size = 4096, .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "X-Api-Key", c->api_key);
    esp_http_client_set_header(cli, "X-Api-Resource-Id",
                               c->resource_id[0] ? c->resource_id : "seed-tts-2.0");
    /* If auth ever fails with an opaque error, add the fixed constant
     * X-Api-App-Key: aGjiRDfUWi (the verified plan endpoint works without it). */
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "tts http err=%s status=%d: %.160s", esp_err_to_name(err), status, resp.buf);
        free(resp.buf);
        return ESP_FAIL;
    }

    /* Harvest every "data":"<base64>" chunk and decode into one PCM buffer. Each
     * chunk is a complete, padded base64 blob; base64 has no '"', so the next
     * quote ends it — robust to the missing delimiter between objects. Total
     * decoded PCM is <= 3/4 of the response, a safe allocation bound. */
    size_t pcm_cap = (size_t)resp.len * 3 / 4 + 16;
    int16_t *pcm = heap_caps_malloc(pcm_cap, MALLOC_CAP_SPIRAM);
    if (!pcm) {
        free(resp.buf);
        return ESP_ERR_NO_MEM;
    }
    size_t pcm_off = 0;   /* bytes written */
    for (char *p = strstr(resp.buf, "\"data\":\""); p; p = strstr(p, "\"data\":\"")) {
        p += 8;
        char *end = strchr(p, '"');
        if (!end) {
            break;
        }
        size_t b64n = (size_t)(end - p);
        if (b64n > 0 && pcm_off < pcm_cap) {
            size_t olen = 0;
            if (mbedtls_base64_decode((unsigned char *)pcm + pcm_off, pcm_cap - pcm_off,
                                      &olen, (const unsigned char *)p, b64n) == 0) {
                pcm_off += olen;
            }
        }
        p = end + 1;
    }

    if (pcm_off == 0) {
        ESP_LOGE(TAG, "tts: no audio chunks: %.200s", resp.buf);
        free(resp.buf);
        free(pcm);
        return ESP_FAIL;
    }
    free(resp.buf);
    *pcm_out = pcm;
    *samples_out = pcm_off / sizeof(int16_t);
    ESP_LOGI(TAG, "tts volc %u samples @%dHz", (unsigned)*samples_out, out_rate);
    return ESP_OK;
}

esp_err_t llm_tts(const char *text, int out_rate, int16_t **pcm_out, size_t *samples_out)
{
    const ns_tts_cfg_t *c = &ns_config_get()->tts;
    if (strcmp(c->provider, "volc") == 0) {
        return tts_volc(c, text, out_rate, pcm_out, samples_out);
    }
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
