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
    /* volc 方舟 Agent Plan defaults: harmless for openai (only read on
     * provider="volc"); base_url is defaulted per-provider inside llm.c when
     * left blank, so a volc user only writes provider + api_key + voice/model. */
    strcpy(cfg->stt.resource_id, "volc.seedasr.sauc.duration");  /* seed-asr-2.0 (ASR 后补) */
    strcpy(cfg->tts.resource_id, "seed-tts-2.0");

    cfg->vision.rotate = 270; /* 实测: 模块横装, CCW 270° 后画面正 (2026-07-09 真人快照标定) */

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

    cfg->protect.stall_ma = 350;
    cfg->protect.stall_ms = 200;
    cfg->protect.stall_retry = 3;

    cfg->move.approach_budget_cm = 15;
    cfg->move.nudge_cm = 4;
    cfg->move.ramp_ms = 100;
    cfg->move.jitter_pct = 15;

    cfg->motion.enabled = false;
    cfg->motion.max_duty_pct = 40;
    /* 底盘校准默认值（docs/14）：闭环默认关，A2/A3 实测后回填并翻开。
     * kv 初值 = 1023/rpm_max（假设 duty∝rpm 的名义斜率），ks 未标定为 0。 */
    cfg->motion.calib.closed_loop = false;
    cfg->motion.calib.wheel_d_mm = 70.0f;    /* Captain 口径轮中心直径，A3 滚动实测修正 */
    cfg->motion.calib.body_r_mm = 56.0f;     /* 外壳 CAD 占位，A3 自旋实测修正 */
    cfg->motion.calib.rpm_max = 15000.0f;    /* 电机轴：136rpm(输出轴,空载6V)×118≈16k，留裕量 */
    for (int i = 0; i < 3; i++) {
        cfg->motion.calib.ks[i] = 0.0f;
        cfg->motion.calib.kv[i] = 1023.0f / 15000.0f;
    }
    cfg->motion.calib.pid_kp = 0.05f;   /* 一阶模型仿真：稳定~260ms/超调~15%/余差<1% */
    cfg->motion.calib.pid_ki = 0.02f;
    cfg->motion.calib.pid_kd = 0.0f;
    cfg->motion.calib.sp_deadband_pct = 3.0f;

    cfg->companion.enabled = true;

    cfg->audio.volume = 70;
    strcpy(cfg->audio.wake_word, "小王");   /* 唤醒词:识别文本含它才应答 */
    cfg->audio.follow_window_s = 8;         /* 应答后 8s 内免唤醒词续聊 */

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

/* Overlay a 3-element float array; partial/short arrays leave dst untouched. */
static void ov_f3(const cJSON *obj, const char *key, float dst[3])
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsArray(it) && cJSON_GetArraySize(it) == 3) {
        for (int i = 0; i < 3; i++) {
            const cJSON *v = cJSON_GetArrayItem(it, i);
            if (cJSON_IsNumber(v)) {
                dst[i] = (float)v->valuedouble;
            }
        }
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
        ov_str(o, "provider", cfg->stt.provider, sizeof(cfg->stt.provider));
        ov_str(o, "base_url", cfg->stt.base_url, sizeof(cfg->stt.base_url));
        ov_str(o, "api_key", cfg->stt.api_key, sizeof(cfg->stt.api_key));
        ov_str(o, "model", cfg->stt.model, sizeof(cfg->stt.model));
        ov_str(o, "resource_id", cfg->stt.resource_id, sizeof(cfg->stt.resource_id));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "tts"))) {
        ov_str(o, "provider", cfg->tts.provider, sizeof(cfg->tts.provider));
        ov_str(o, "base_url", cfg->tts.base_url, sizeof(cfg->tts.base_url));
        ov_str(o, "api_key", cfg->tts.api_key, sizeof(cfg->tts.api_key));
        ov_str(o, "model", cfg->tts.model, sizeof(cfg->tts.model));
        ov_str(o, "voice", cfg->tts.voice, sizeof(cfg->tts.voice));
        ov_str(o, "resource_id", cfg->tts.resource_id, sizeof(cfg->tts.resource_id));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "vision"))) {
        ov_int(o, "rotate", &cfg->vision.rotate);
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
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "protect"))) {
        ov_int(o, "stall_ma", &cfg->protect.stall_ma);
        ov_int(o, "stall_ms", &cfg->protect.stall_ms);
        ov_int(o, "stall_retry", &cfg->protect.stall_retry);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "move"))) {
        ov_int(o, "approach_budget_cm", &cfg->move.approach_budget_cm);
        ov_int(o, "nudge_cm", &cfg->move.nudge_cm);
        ov_int(o, "ramp_ms", &cfg->move.ramp_ms);
        ov_int(o, "jitter_pct", &cfg->move.jitter_pct);
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "motion"))) {
        ov_bool(o, "enabled", &cfg->motion.enabled);
        ov_int(o, "max_duty_pct", &cfg->motion.max_duty_pct);
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(o, "calib");
        if (c) {
            ov_bool(c, "closed_loop", &cfg->motion.calib.closed_loop);
            ov_float(c, "wheel_d_mm", &cfg->motion.calib.wheel_d_mm);
            ov_float(c, "body_r_mm", &cfg->motion.calib.body_r_mm);
            ov_float(c, "rpm_max", &cfg->motion.calib.rpm_max);
            ov_f3(c, "ks", cfg->motion.calib.ks);
            ov_f3(c, "kv", cfg->motion.calib.kv);
            ov_float(c, "sp_deadband_pct", &cfg->motion.calib.sp_deadband_pct);
            const cJSON *p = cJSON_GetObjectItemCaseSensitive(c, "pid");
            if (p) {
                ov_float(p, "kp", &cfg->motion.calib.pid_kp);
                ov_float(p, "ki", &cfg->motion.calib.pid_ki);
                ov_float(p, "kd", &cfg->motion.calib.pid_kd);
            }
        }
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "companion"))) {
        ov_bool(o, "enabled", &cfg->companion.enabled);
        ov_str(o, "token", cfg->companion.token, sizeof(cfg->companion.token));
    }
    if ((o = cJSON_GetObjectItemCaseSensitive(root, "audio"))) {
        ov_int(o, "volume", &cfg->audio.volume);
        if (cfg->audio.volume < 0)   cfg->audio.volume = 0;
        if (cfg->audio.volume > 100) cfg->audio.volume = 100;
        ov_str(o, "wake_word", cfg->audio.wake_word, sizeof(cfg->audio.wake_word));
        ov_int(o, "follow_window_s", &cfg->audio.follow_window_s);
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
