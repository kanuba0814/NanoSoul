#include "volc_asr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "cJSON.h"
#include "miniz.h"

static const char *TAG = "volc_asr";

/* ---- protocol constants (火山 sauc bigmodel 单流, 大端二进制分帧) ----
 * 4-byte header: b0 = (proto_v1<<4)|hdr_size(=1, ×4=4B); b1 = msg_type<<4|flags;
 * b2 = serialization<<4|compression; b3 = 0. flags 带 sequence 位时头后跟 4B 大端
 * seq, 再 4B 大端 payload_size, 最后 gzip 后的 payload. */
#define MT_FULL_CLIENT   0x1   /* FULL_CLIENT_REQUEST  */
#define MT_AUDIO_ONLY    0x2   /* AUDIO_ONLY_REQUEST   */
#define MT_FULL_RESPONSE 0x9   /* server: FULL_RESPONSE */
#define MT_ERROR_RESP    0xf   /* server: ERROR_RESPONSE */

#define FLAG_POS_SEQ     0x1   /* payload 带正 seq            */
#define FLAG_NEG_SEQ     0x3   /* 末帧: 带 seq 且 seq 取负     */
#define FLAG_LAST_PKG    0x2   /* server: is_last_package    */
#define FLAG_EVENT       0x4   /* server: 4B event 字段        */

#define SER_NONE         0x0
#define SER_JSON         0x1
#define COMP_NONE        0x0
#define COMP_GZIP        0x1

#define AUDIO_FRAME_BYTES 6400 /* 200ms @16k/16bit/mono */
#define RX_CAP            (32 * 1024)
#define DEFAULT_URI \
    "wss://openspeech.bytedance.com/api/v3/plan/sauc/bigmodel_nostream"

#define BIT_CONNECTED (1 << 0)
#define BIT_DONE      (1 << 1)
#define BIT_ERR       (1 << 2)

typedef struct {
    EventGroupHandle_t evt;
    uint8_t           *rx;      /* reassembly buffer (PSRAM, RX_CAP) */
    size_t             rx_len;  /* expected payload_len of current message */
    size_t             rx_off;  /* bytes gathered so far */
    char              *out;     /* caller's text buffer */
    size_t             out_cap;
    int                error_code;
} asr_ctx_t;

/* ---- byte helpers ---- */
static void be32_put(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint32_t be32_get(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ---- gzip via RFC1951 "stored" (uncompressed) deflate blocks ----
 * The server only requires gzip FRAMING; PCM audio and a 241-byte config gain
 * nothing from actual compression. Stored blocks are deterministic and skip
 * miniz's tdefl compressor entirely (which allocates a ~312KB state per call
 * and failed opaquely on-device — see git history). miniz is kept for tinfl
 * (response inflate) + mz_crc32 only. */
static uint8_t *gzip_pack(const void *src, size_t len, size_t *out_len)
{
    size_t nblk = len ? (len + 65534) / 65535 : 1;
    size_t total = 10 + nblk * 5 + len + 8;   /* hdr + block headers + data + trailer */
    uint8_t *g = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!g) {
        return NULL;
    }
    static const uint8_t hdr[10] = { 0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0x00, 0x03 };
    memcpy(g, hdr, 10);
    size_t o = 10, off = 0;
    do {
        size_t chunk = (len - off > 65535) ? 65535 : len - off;
        g[o++] = (off + chunk >= len) ? 1 : 0;         /* BFINAL, BTYPE=00 (stored) */
        g[o++] = chunk & 0xff;  g[o++] = (chunk >> 8) & 0xff;
        g[o++] = ~chunk & 0xff; g[o++] = (~chunk >> 8) & 0xff;
        memcpy(g + o, (const uint8_t *)src + off, chunk);
        o += chunk;
        off += chunk;
    } while (off < len);
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, src, len);
    uint32_t isize = (uint32_t)len;   /* mod 2^32 per RFC1952 */
    uint8_t *t = g + o;               /* little-endian trailer */
    t[0] = crc; t[1] = crc >> 8; t[2] = crc >> 16; t[3] = crc >> 24;
    t[4] = isize; t[5] = isize >> 8; t[6] = isize >> 16; t[7] = isize >> 24;
    *out_len = o + 8;
    return g;
}

/* Inflate a gzip member; caller frees with free(). Handles FLG optional fields
 * defensively (server responses are normally FLG=0). Returns NULL on any fault. */
static uint8_t *gzip_unpack(const uint8_t *src, size_t len, size_t *out_len)
{
    if (len < 18 || src[0] != 0x1f || src[1] != 0x8b) {
        return NULL;
    }
    uint8_t flg = src[3];
    size_t pos = 10;
    if (flg & 0x04) {                              /* FEXTRA */
        if (pos + 2 > len) return NULL;
        uint16_t xlen = src[pos] | (src[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { while (pos < len && src[pos]) pos++; pos++; }  /* FNAME */
    if (flg & 0x10) { while (pos < len && src[pos]) pos++; pos++; }  /* FCOMMENT */
    if (flg & 0x02) { pos += 2; }                                    /* FHCRC */
    if (pos + 8 > len) {
        return NULL;
    }
    size_t deflate_len = len - pos - 8;            /* drop 8B crc32+isize trailer */
    return tinfl_decompress_mem_to_heap(src + pos, deflate_len, out_len, 0);
}

/* Build a client frame into PSRAM; caller frees with free(). */
static uint8_t *build_frame(uint8_t msg_type, uint8_t flags, uint8_t serialization,
                            uint8_t compression, int32_t seq, const uint8_t *payload,
                            size_t payload_len, size_t *frame_len)
{
    bool has_seq = (flags & FLAG_POS_SEQ);
    size_t total = 4 + (has_seq ? 4 : 0) + 4 + payload_len;
    uint8_t *f = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!f) {
        return NULL;
    }
    f[0] = (1 << 4) | 1;                                   /* proto v1 | hdr_size 1 */
    f[1] = (msg_type << 4) | (flags & 0x0f);
    f[2] = (serialization << 4) | (compression & 0x0f);
    f[3] = 0x00;
    size_t o = 4;
    if (has_seq) { be32_put(f + o, (uint32_t)seq); o += 4; }
    be32_put(f + o, (uint32_t)payload_len); o += 4;
    memcpy(f + o, payload, payload_len);
    *frame_len = total;
    return f;
}

/* ---- response parse (runs in the websocket task via the event handler) ----
 * The endpoint streams one interim response per audio frame (text fills in as
 * recognition progresses) and marks the final one with is_last_package. Interim
 * responses only refresh the best-so-far text; only the FINAL package decides. */
static void parse_result_json(asr_ctx_t *c, const char *json, size_t len, bool is_last)
{
    int log_n = (int)(len > 200 ? 200 : len);   /* json isn't NUL-terminated */
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "result JSON parse failed: %.*s", log_n, json);
        if (is_last) {
            xEventGroupSetBits(c->evt, BIT_ERR);
        }
        return;
    }
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (result) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(result, "text");
        if (cJSON_IsString(text) && text->valuestring[0]) {
            strlcpy(c->out, text->valuestring, c->out_cap);
        } else {
            cJSON *utts = cJSON_GetObjectItemCaseSensitive(result, "utterances");
            if (cJSON_IsArray(utts) && cJSON_GetArraySize(utts) > 0) {
                c->out[0] = '\0';
                cJSON *u;
                cJSON_ArrayForEach(u, utts) {
                    cJSON *ut = cJSON_GetObjectItemCaseSensitive(u, "text");
                    if (cJSON_IsString(ut)) {
                        strlcat(c->out, ut->valuestring, c->out_cap);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
    if (!is_last) {
        return;   /* interim: keep listening for the final package */
    }
    /* Final package with no text = the service heard no speech (false VAD wake).
     * That is a SUCCESSFUL recognition of nothing — the caller decides to drop
     * it silently. Only transport/server errors report failure. */
    if (c->out[0] == '\0') {
        ESP_LOGI(TAG, "final: no speech recognized");
    }
    xEventGroupSetBits(c->evt, BIT_DONE);
}

static void parse_frame(asr_ctx_t *c)
{
    const uint8_t *b = c->rx;
    size_t n = c->rx_len;
    if (n < 4) {
        ESP_LOGE(TAG, "short frame %u", (unsigned)n);
        xEventGroupSetBits(c->evt, BIT_ERR);
        return;
    }
    uint8_t hdr_words   = b[0] & 0x0f;
    uint8_t msg_type    = b[1] >> 4;
    uint8_t flags       = b[1] & 0x0f;
    uint8_t compression = b[2] & 0x0f;
    bool is_last        = (flags & FLAG_LAST_PKG) != 0;
    size_t p = (size_t)hdr_words * 4;
    if ((flags & FLAG_POS_SEQ)) p += 4;   /* payload_sequence (unused here) */
    if ((flags & FLAG_EVENT))   p += 4;   /* event (unused here) */

    int32_t error_code = 0;
    if (msg_type == MT_ERROR_RESP) {
        if (p + 4 > n) { xEventGroupSetBits(c->evt, BIT_ERR); return; }
        error_code = (int32_t)be32_get(b + p); p += 4;
    } else if (msg_type != MT_FULL_RESPONSE) {
        ESP_LOGE(TAG, "unexpected msg_type=0x%x flags=0x%x", msg_type, flags);
        xEventGroupSetBits(c->evt, BIT_ERR);
        return;
    }
    if (p + 4 > n) { xEventGroupSetBits(c->evt, BIT_ERR); return; }
    uint32_t psize = be32_get(b + p); p += 4;
    if (p + psize > n) {
        psize = (uint32_t)(n - p);   /* clamp to what we actually gathered */
    }
    const uint8_t *payload = b + p;

    uint8_t *dec = NULL;
    const char *json = (const char *)payload;
    size_t json_len = psize;
    if (compression == COMP_GZIP) {
        dec = gzip_unpack(payload, psize, &json_len);
        if (!dec) {
            ESP_LOGE(TAG, "gunzip failed (msg_type=0x%x, %u bytes)", msg_type, (unsigned)psize);
            xEventGroupSetBits(c->evt, BIT_ERR);
            return;
        }
        json = (const char *)dec;
    }

    if (msg_type == MT_ERROR_RESP) {
        ESP_LOGE(TAG, "ERROR_RESPONSE code=%d: %.*s", error_code,
                 (int)(json_len > 200 ? 200 : json_len), json);
        c->error_code = error_code;
        xEventGroupSetBits(c->evt, BIT_ERR);
    } else {
        parse_result_json(c, json, json_len, is_last);
    }
    free(dec);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    asr_ctx_t *c = arg;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(c->evt, BIT_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d->op_code == 0x08) {   /* server CLOSE: first 2 bytes = BE status code */
            int code = (d->data_len >= 2)
                           ? (((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1]) : 0;
            ESP_LOGW(TAG, "server closed ws: code=%d reason=%.*s", code,
                     d->data_len > 2 ? d->data_len - 2 : 0,
                     d->data_len > 2 ? d->data_ptr + 2 : "");
            /* code 1000 = the session ran to completion (with or without any
             * recognized speech) — report success and let the accumulated text
             * (possibly empty) speak for itself. Abnormal codes are errors. */
            xEventGroupSetBits(c->evt, (code == 1000 || c->out[0]) ? BIT_DONE : BIT_ERR);
            break;
        }
        if (d->op_code != 0x02 && d->op_code != 0x00) {  /* only binary + continuation */
            ESP_LOGW(TAG, "ignoring ws frame op=0x%x len=%d", d->op_code, d->data_len);
            break;
        }
        if (d->payload_len <= 0) {
            break;
        }
        if (d->payload_offset == 0) {
            c->rx_off = 0;
            c->rx_len = d->payload_len;
            if (c->rx_len > RX_CAP) {
                ESP_LOGE(TAG, "response %u > RX_CAP", (unsigned)c->rx_len);
                xEventGroupSetBits(c->evt, BIT_ERR);
                c->rx_len = 0;
                break;
            }
        }
        if (c->rx && c->rx_len && d->payload_offset + d->data_len <= RX_CAP) {
            memcpy(c->rx + d->payload_offset, d->data_ptr, d->data_len);
            c->rx_off = d->payload_offset + d->data_len;
            if (c->rx_off >= c->rx_len) {
                parse_frame(c);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "websocket transport error");
        xEventGroupSetBits(c->evt, BIT_ERR);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        /* Harmless once BIT_DONE is set (main checks DONE first); salvages
         * accumulated interim text if the link drops without a final package. */
        ESP_LOGW(TAG, "ws %s (text so far: %s)",
                 id == WEBSOCKET_EVENT_CLOSED ? "closed" : "disconnected",
                 c->out[0] ? "yes" : "none");
        xEventGroupSetBits(c->evt, c->out[0] ? BIT_DONE : BIT_ERR);
        break;
    default:
        break;
    }
}

/* Send the FULL_CLIENT_REQUEST (JSON config, gzip). Returns ESP_OK on send. */
static esp_err_t send_config(esp_websocket_client_handle_t cli, int sample_rate)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *user = cJSON_AddObjectToObject(root, "user");
    cJSON_AddStringToObject(user, "uid", "nanosoul");
    cJSON *audio = cJSON_AddObjectToObject(root, "audio");
    cJSON_AddStringToObject(audio, "format", "pcm");
    cJSON_AddStringToObject(audio, "codec", "raw");
    cJSON_AddNumberToObject(audio, "rate", sample_rate);
    cJSON_AddNumberToObject(audio, "bits", 16);
    cJSON_AddNumberToObject(audio, "channel", 1);
    cJSON *req = cJSON_AddObjectToObject(root, "request");
    cJSON_AddStringToObject(req, "model_name", "bigmodel");
    cJSON_AddBoolToObject(req, "enable_itn", true);
    cJSON_AddBoolToObject(req, "enable_punc", true);
    cJSON_AddBoolToObject(req, "enable_ddc", true);
    cJSON_AddBoolToObject(req, "show_utterances", false);
    cJSON_AddBoolToObject(req, "enable_nonstream", false);
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!js) {
        return ESP_ERR_NO_MEM;
    }

    size_t glen = 0;
    uint8_t *payload = gzip_pack(js, strlen(js), &glen);
    size_t jlen = strlen(js);
    free(js);
    if (!payload) {
        ESP_LOGE(TAG, "gzip_pack(config, %u bytes) failed", (unsigned)jlen);
        return ESP_ERR_NO_MEM;
    }
    size_t flen = 0;
    uint8_t *frame = build_frame(MT_FULL_CLIENT, FLAG_POS_SEQ, SER_JSON, COMP_GZIP,
                                 1, payload, glen, &flen);
    free(payload);
    if (!frame) {
        ESP_LOGE(TAG, "build_frame(config) alloc failed");
        return ESP_ERR_NO_MEM;
    }
    int sent = esp_websocket_client_send_bin(cli, (const char *)frame, flen, pdMS_TO_TICKS(5000));
    free(frame);
    if (sent < 0) {
        ESP_LOGE(TAG, "send config failed (%u->%u bytes)", (unsigned)jlen, (unsigned)glen);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "config sent (%u->%u bytes gzip)", (unsigned)jlen, (unsigned)glen);
    return ESP_OK;
}

/* Stream the PCM as AUDIO_ONLY frames; the final frame carries NEG seq. */
static esp_err_t send_audio(esp_websocket_client_handle_t cli, const int16_t *pcm, size_t samples)
{
    const uint8_t *audio = (const uint8_t *)pcm;
    size_t total = samples * sizeof(int16_t);
    int32_t seq = 1;   /* config was seq 1; audio frames start at 2 */
    size_t off = 0;
    bool sent_last = false;
    do {
        size_t chunk = total - off;
        if (chunk > AUDIO_FRAME_BYTES) {
            chunk = AUDIO_FRAME_BYTES;
        }
        bool last = (off + chunk >= total);
        seq++;
        uint8_t flags = last ? FLAG_NEG_SEQ : FLAG_POS_SEQ;
        int32_t frame_seq = last ? -seq : seq;

        size_t glen = 0;
        uint8_t *payload = gzip_pack(audio + off, chunk, &glen);
        if (!payload) {
            ESP_LOGE(TAG, "gzip_pack(audio seq=%d, %u bytes) failed", frame_seq, (unsigned)chunk);
            return ESP_ERR_NO_MEM;
        }
        size_t flen = 0;
        uint8_t *frame = build_frame(MT_AUDIO_ONLY, flags, SER_NONE, COMP_GZIP,
                                     frame_seq, payload, glen, &flen);
        free(payload);
        if (!frame) {
            ESP_LOGE(TAG, "build_frame(audio seq=%d) alloc failed", frame_seq);
            return ESP_ERR_NO_MEM;
        }
        int sent = esp_websocket_client_send_bin(cli, (const char *)frame, flen, pdMS_TO_TICKS(5000));
        free(frame);
        if (sent < 0) {
            ESP_LOGE(TAG, "send audio frame seq=%d failed", frame_seq);
            return ESP_FAIL;
        }
        off += chunk;
        sent_last = last;
    } while (!sent_last);
    return ESP_OK;
}

esp_err_t volc_asr_recognize(const ns_stt_cfg_t *cfg, const int16_t *pcm, size_t samples,
                             int sample_rate, char *text, size_t text_cap)
{
    if (!cfg || !text || text_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    text[0] = '\0';
    if (cfg->api_key[0] == '\0') {
        ESP_LOGE(TAG, "no stt api_key configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* Waveform sanity for the ASR payload: clipping, DC offset or a byte-swap
     * all show up here long before the server's empty-text tells us nothing. */
    {
        int16_t mn = 0, mx = 0;
        int64_t acc = 0, sum = 0;
        for (size_t i = 0; i < samples; i++) {
            int16_t v = pcm[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
            acc += (int32_t)v * v;
        }
        ESP_LOGI(TAG, "pcm %u samples: min=%d max=%d mean=%d rms=%d",
                 (unsigned)samples, mn, mx,
                 (int)(samples ? sum / (int64_t)samples : 0),
                 (int)(samples ? (int64_t)sqrtf((float)(acc / (int64_t)samples)) : 0));
    }

    asr_ctx_t ctx = { .out = text, .out_cap = text_cap };
    ctx.evt = xEventGroupCreate();
    ctx.rx = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM);
    if (!ctx.evt || !ctx.rx) {
        if (ctx.evt) vEventGroupDelete(ctx.evt);
        free(ctx.rx);
        return ESP_ERR_NO_MEM;
    }

    /* Unique request/connect id (hex of on-chip RNG); never logs the key. */
    char rid[33];
    snprintf(rid, sizeof(rid), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());

    char headers[512];
    snprintf(headers, sizeof(headers),
             "X-Api-Key: %s\r\n"
             "X-Api-Resource-Id: %s\r\n"
             "X-Api-Request-Id: %s\r\n"
             "X-Api-Connect-Id: %s\r\n"
             "X-Api-Sequence: -1\r\n",
             cfg->api_key,
             cfg->resource_id[0] ? cfg->resource_id : "volc.seedasr.sauc.duration",
             rid, rid);

    esp_websocket_client_config_t wcfg = {
        .uri = cfg->base_url[0] ? cfg->base_url : DEFAULT_URI,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_reconnect = true,
        .headers = headers,
        .buffer_size = 8192,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 10000,
    };
    esp_websocket_client_handle_t cli = esp_websocket_client_init(&wcfg);
    if (!cli) {
        ESP_LOGE(TAG, "websocket init failed");
        vEventGroupDelete(ctx.evt);
        free(ctx.rx);
        return ESP_FAIL;
    }
    esp_websocket_register_events(cli, WEBSOCKET_EVENT_ANY, ws_event, &ctx);

    esp_err_t ret = ESP_FAIL;
    if (esp_websocket_client_start(cli) != ESP_OK) {
        ESP_LOGE(TAG, "websocket start failed");
        goto cleanup;
    }

    /* Handshake: wait for CONNECTED (5s). A non-101 handshake surfaces as ERROR. */
    EventBits_t bits = xEventGroupWaitBits(ctx.evt, BIT_CONNECTED | BIT_ERR,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    if (!(bits & BIT_CONNECTED)) {
        ESP_LOGE(TAG, "connect timeout/handshake failed (bits=0x%lx)", (unsigned long)bits);
        ret = ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    if (send_config(cli, sample_rate) != ESP_OK) {
        goto cleanup;
    }
    if (send_audio(cli, pcm, samples) != ESP_OK) {
        goto cleanup;
    }
    ESP_LOGI(TAG, "sent %u ms audio, awaiting result",
             (unsigned)(samples * 1000 / (sample_rate ? sample_rate : 16000)));

    /* Single-stream: one final result within the 15s server limit. */
    bits = xEventGroupWaitBits(ctx.evt, BIT_DONE | BIT_ERR, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(15000));
    if (bits & BIT_DONE) {
        ret = ESP_OK;
        if (text[0]) {
            ESP_LOGI(TAG, "heard: %.60s", text);
        }
    } else if (bits & BIT_ERR) {
        ret = ESP_FAIL;   /* specific cause already logged in parse_frame/ws_event */
    } else {
        ESP_LOGE(TAG, "result timeout (15s, no final package)");
        ret = ESP_ERR_TIMEOUT;
    }

cleanup:
    esp_websocket_client_close(cli, pdMS_TO_TICKS(1000));
    esp_websocket_client_destroy(cli);
    vEventGroupDelete(ctx.evt);
    free(ctx.rx);
    if (ret != ESP_OK) {
        text[0] = '\0';
    }
    return ret;
}
