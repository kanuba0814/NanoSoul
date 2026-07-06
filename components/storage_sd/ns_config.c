#include "ns_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "ns_config";

static ns_config_t s_cfg;
static bool        s_inited;

void ns_config_defaults(ns_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    /* chat: Anthropic-compatible by default */
    strcpy(cfg->chat.provider, "anthropic");
    strcpy(cfg->chat.base_url, "https://api.anthropic.com");
    strcpy(cfg->chat.model, "claude-opus-4-8");
    cfg->chat.max_tokens = 512;
    strcpy(cfg->chat.system_prompt,
           "You are NanoSoul, a small desktop companion robot. "
           "Reply briefly and warmly in the user's language.");

    strcpy(cfg->stt.model, "whisper-1");
    strcpy(cfg->tts.model, "tts-1");
    strcpy(cfg->tts.voice, "alloy");

    cfg->behavior.near_lo = 0.04f;
    cfg->behavior.near_hi = 0.18f;
    cfg->behavior.frontal_thresh = 0.70f;
    cfg->behavior.gaze_hold_ms = 1500;
    cfg->behavior.gaze_cooldown_s = 30;
    cfg->behavior.idle_scan = false;
    cfg->behavior.autonomy = true;
    cfg->behavior.autonomy_idle_s = 90;

    cfg->mood.enabled = true;
    cfg->mood.energy_init = 0.5f;
    cfg->mood.social_init = 0.5f;
    cfg->mood.social_tau_min = 10;

    cfg->pc.stale_s = 15;
    cfg->pc.respect_dnd = true;
    cfg->pc.quiet_work = true;
    cfg->pc.quiet_meeting = true;
    cfg->pc.invite_idle_s = 300;
    cfg->pc.invite_cooldown_min = 60;

    cfg->light.dark_lux = 10;
    cfg->light.bright_lux = 30;
    cfg->light.dark_hold_s = 30;

    cfg->imu.tap_th = 3.0f;
    cfg->imu.lift_g_dev = 0.15f;
    cfg->imu.lift_hold_ms = 300;
    cfg->imu.place_still_ms = 1500;
    cfg->imu.tilt_deg = 8;

    cfg->motion.enabled = false;
    cfg->motion.max_duty_pct = 40;

    cfg->companion.enabled = true;

    cfg->debug.overlay = true;
    strcpy(cfg->debug.log_level, "info");

    strcpy(cfg->source, "default");
}

/* --- small cJSON overlay helpers: only touch dst when the key is present --- */

static void ov_str(const cJSON *obj, const char *key, char *dst, size_t n)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        strlcpy(dst, it->valuestring, n);
    }
}

static void ov_int(const cJSON *obj, const char *key, int *dst)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(it)) {
        *dst = it->valueint;
    }
}

static void ov_float(const cJSON *obj, const char *key, float *dst)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(it)) {
        *dst = (float)it->valuedouble;
    }
}

static void ov_bool(const cJSON *obj, const char *key, bool *dst)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) {
        *dst = cJSON_IsTrue(it);
    }
}

static void apply_json(const cJSON *root, ns_config_t *cfg)
{
    const cJSON *o;

    if ((o = cJSON_GetObjectItemCaseSensitive(root, "wifi"))) {
        ov_str(o, "ssid", cfg->wifi.ssid, sizeof(cfg->wifi.ssid));
        ov_str(o, "password", cfg->wifi.password, sizeof(cfg->wifi.password));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "chat"))) {
        ov_str(o, "provider", cfg->chat.provider, sizeof(cfg->chat.provider));
        ov_str(o, "base_url", cfg->chat.base_url, sizeof(cfg->chat.base_url));
        ov_str(o, "api_key", cfg->chat.api_key, sizeof(cfg->chat.api_key));
        ov_str(o, "model", cfg->chat.model, sizeof(cfg->chat.model));
        ov_int(o, "max_tokens", &cfg->chat.max_tokens);
        ov_str(o, "system_prompt", cfg->chat.system_prompt, sizeof(cfg->chat.system_prompt));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "stt"))) {
        ov_str(o, "base_url", cfg->stt.base_url, sizeof(cfg->stt.base_url));
        ov_str(o, "api_key", cfg->stt.api_key, sizeof(cfg->stt.api_key));
        ov_str(o, "model", cfg->stt.model, sizeof(cfg->stt.model));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "tts"))) {
        ov_str(o, "base_url", cfg->tts.base_url, sizeof(cfg->tts.base_url));
        ov_str(o, "api_key", cfg->tts.api_key, sizeof(cfg->tts.api_key));
        ov_str(o, "model", cfg->tts.model, sizeof(cfg->tts.model));
        ov_str(o, "voice", cfg->tts.voice, sizeof(cfg->tts.voice));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "behavior"))) {
        ov_float(o, "near_lo", &cfg->behavior.near_lo);
        ov_float(o, "near_hi", &cfg->behavior.near_hi);
        ov_float(o, "frontal_thresh", &cfg->behavior.frontal_thresh);
        ov_int(o, "gaze_hold_ms", &cfg->behavior.gaze_hold_ms);
        ov_int(o, "gaze_cooldown_s", &cfg->behavior.gaze_cooldown_s);
        ov_bool(o, "idle_scan", &cfg->behavior.idle_scan);
        ov_bool(o, "autonomy", &cfg->behavior.autonomy);
        ov_int(o, "autonomy_idle_s", &cfg->behavior.autonomy_idle_s);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "mood"))) {
        ov_bool(o, "enabled", &cfg->mood.enabled);
        ov_float(o, "energy_init", &cfg->mood.energy_init);
        ov_float(o, "social_init", &cfg->mood.social_init);
        ov_int(o, "social_tau_min", &cfg->mood.social_tau_min);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "pc"))) {
        ov_int(o, "stale_s", &cfg->pc.stale_s);
        ov_bool(o, "respect_dnd", &cfg->pc.respect_dnd);
        ov_bool(o, "quiet_work", &cfg->pc.quiet_work);
        ov_bool(o, "quiet_meeting", &cfg->pc.quiet_meeting);
        ov_int(o, "invite_idle_s", &cfg->pc.invite_idle_s);
        ov_int(o, "invite_cooldown_min", &cfg->pc.invite_cooldown_min);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "light"))) {
        ov_int(o, "dark_lux", &cfg->light.dark_lux);
        ov_int(o, "bright_lux", &cfg->light.bright_lux);
        ov_int(o, "dark_hold_s", &cfg->light.dark_hold_s);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "imu"))) {
        ov_float(o, "tap_th", &cfg->imu.tap_th);
        ov_float(o, "lift_g_dev", &cfg->imu.lift_g_dev);
        ov_int(o, "lift_hold_ms", &cfg->imu.lift_hold_ms);
        ov_int(o, "place_still_ms", &cfg->imu.place_still_ms);
        ov_int(o, "tilt_deg", &cfg->imu.tilt_deg);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "motion"))) {
        ov_bool(o, "enabled", &cfg->motion.enabled);
        ov_int(o, "max_duty_pct", &cfg->motion.max_duty_pct);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "companion"))) {
        ov_bool(o, "enabled", &cfg->companion.enabled);
        ov_str(o, "token", cfg->companion.token, sizeof(cfg->companion.token));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "debug"))) {
        ov_bool(o, "overlay", &cfg->debug.overlay);
        ov_str(o, "log_level", cfg->debug.log_level, sizeof(cfg->debug.log_level));
    }
}

esp_err_t ns_config_load(const char *path, ns_config_t *cfg)
{
    ns_config_defaults(cfg);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "%s not found; using defaults", path);
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 32 * 1024) {
        fclose(f);
        ESP_LOGW(TAG, "config size %ld out of range; using defaults", len);
        return ESP_OK;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t rd = fread(buf, 1, len, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "config JSON parse failed; using defaults");
        return ESP_OK; /* still boots on defaults */
    }

    apply_json(root, cfg);
    cJSON_Delete(root);
    strcpy(cfg->source, "sd");
    ESP_LOGI(TAG, "loaded config from %s (chat provider=%s model=%s, motion=%s)",
             path, cfg->chat.provider, cfg->chat.model,
             cfg->motion.enabled ? "on" : "off");
    return ESP_OK;
}

esp_err_t ns_config_init(const char *path)
{
    esp_err_t err = ns_config_load(path, &s_cfg);
    s_inited = true;
    return err;
}

const ns_config_t *ns_config_get(void)
{
    if (!s_inited) {
        ns_config_defaults(&s_cfg);
        s_inited = true;
    }
    return &s_cfg;
}
