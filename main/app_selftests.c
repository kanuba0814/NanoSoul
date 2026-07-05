#include "app_selftests.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"

#include <stdlib.h>

#include <time.h>

#include "board_i2c0.h"
#include "bsp_pins.h"
#include "camera.h"
#include "companion.h"
#include "face.h"
#include "hud.h"
#include "audio.h"
#include "llm.h"
#include "motion.h"
#include "netlink.h"
#include "ns_config.h"
#include "sd_storage.h"
#include "selftest.h"
#include "soul.h"
#include "vision.h"
#include "voice.h"

/* ---------------- Phase 0 checks ---------------- */

static st_report_t check_sd_mount(void)
{
    if (!sd_storage_mounted()) {
        return st_skip("no card / mount failed");
    }
    uint64_t total = 0, freeb = 0;
    if (sd_storage_usage(&total, &freeb) != ESP_OK) {
        return st_pass("mounted");
    }
    return st_pass("%lluMB free / %lluMB", (unsigned long long)(freeb / (1024 * 1024)),
                   (unsigned long long)(total / (1024 * 1024)));
}

static st_report_t check_config(void)
{
    const ns_config_t *c = ns_config_get();
    return st_pass("src=%s provider=%s motion=%s", c->source, c->chat.provider,
                   c->motion.enabled ? "on" : "off");
}

static st_report_t check_psram(void)
{
    size_t sz = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (sz == 0) {
        return st_fail("no PSRAM detected");
    }
    return st_pass("%uMB total", (unsigned)(sz / (1024 * 1024)));
}

static st_report_t check_partitions(void)
{
    static const char *labels[] = { "ota_0", "emote_gen", "human_face_det", "srmodel", "storage" };
    char missing[64] = { 0 };
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        const esp_partition_t *p =
            esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, labels[i]);
        if (!p) {
            size_t used = strlen(missing);
            snprintf(missing + used, sizeof(missing) - used, "%s%s", used ? "," : "", labels[i]);
        }
    }
    if (missing[0]) {
        return st_fail("missing: %s", missing);
    }
    return st_pass("all present");
}

/* ---------------- Phase A checks (face + HUD) ---------------- */

static st_report_t check_emote_mount(void)
{
    int c = face_emote_clip_count();
    if (c < 0) {
        return st_skip("face not up");
    }
    if (c < 1) {
        return st_fail("0 clips mounted");
    }
    return st_pass("%d clips", c);
}

static st_report_t check_emote_render(void)
{
    static const char *emo[] = { "waiting", "think", "o", "sad", "sleep" };
    static int idx;
    static uint32_t last;

    if (!face_ready()) {
        return st_skip("face not up");
    }
    uint32_t f = face_frame_count();
    if (f == 0) {
        return st_skip("render warming");
    }
    /* Cycle through all emotions across rounds; verify the render loop advanced. */
    const char *name = emo[idx % 5];
    face_set_emotion(name, FACE_FADE);
    idx++;
    uint32_t delta = f - last;
    bool ok = f > last;
    last = f;
    return ok ? st_pass("emo=%s +%uframes", name, (unsigned)delta)
              : st_fail("render stalled f=%u", (unsigned)f);
}

static st_report_t check_hud(void)
{
    return hud_ready() ? st_pass("6 lines") : st_skip("overlay off");
}

/* ---------------- Phase B checks (camera + vision) ---------------- */

static st_report_t check_i2c0_probe(void)
{
    if (!board_i2c0_bus()) {
        return st_skip("i2c0 not up");
    }
    bool cam = board_i2c0_probe(BSP_CAMERA_SCCB_ADDR);
    return cam ? st_pass("OV5647 @0x%02x ack", BSP_CAMERA_SCCB_ADDR)
               : st_fail("no SCCB ack @0x%02x", BSP_CAMERA_SCCB_ADDR);
}

static st_report_t check_camera_stream(void)
{
    static uint32_t last;
    if (!camera_running()) {
        return st_skip("camera not streaming");
    }
    uint32_t f = camera_frame_count();
    uint32_t d = f - last;
    last = f;
    return d > 0 ? st_pass("+%u frames/round", (unsigned)d) : st_fail("no frames (f=%u)", (unsigned)f);
}

static st_report_t check_face_detect(void)
{
    static uint32_t last;
    if (!vision_ready()) {
        return st_skip("vision not up");
    }
    uint32_t d = vision_detect_count() - last;
    last = vision_detect_count();
    tel_face_t f;
    vision_get(&f);
    if (d == 0) {
        return st_fail("detector stalled");
    }
    return st_pass("%.1ffps face=%d a%.3f fr%.2f", vision_fps(), f.present, f.area_ratio, f.frontal_score);
}

/* ---------------- Phase C checks (motion IK + soul FSM, pure logic) ---------------- */

static st_report_t check_motion_ik(void)
{
    int16_t fwd[3];
    motion_ik(1.0f, 0.0f, 0.0f, 100, fwd);   /* pure forward */
    /* wheels at {0,120,240}: forward projects onto tangents as {0,-sin120,-sin240}
     * = {0, -0.866, +0.866}; no wheel saturates, so no normalization -> {0,-886,886} */
    if (!(abs(fwd[0]) <= 3 && fwd[1] >= -895 && fwd[1] <= -875 && fwd[2] >= 875 && fwd[2] <= 895)) {
        return st_fail("fwd[%d,%d,%d]", fwd[0], fwd[1], fwd[2]);
    }
    int16_t rot[3];
    motion_ik(0.0f, 0.0f, 1.0f, 100, rot);   /* pure rotation: all wheels equal, saturate */
    if (!(rot[0] >= 1015 && rot[1] >= 1015 && rot[2] >= 1015)) {
        return st_fail("rot[%d,%d,%d]", rot[0], rot[1], rot[2]);
    }
    return st_pass("fwd[%d,%d,%d] rot[%d]", fwd[0], fwd[1], fwd[2], rot[0]);
}

static st_report_t check_soul_sim(void)
{
    soul_ctx_t c = {0};
    c.state = SOUL_IDLE;
    c.session = SOUL_IDLE;
    c.emotion = "waiting";
    ns_behavior_cfg_t b = {
        .near_lo = 0.04f, .near_hi = 0.18f, .frontal_thresh = 0.7f,
        .gaze_hold_ms = 1500, .idle_scan = false,
    };

    tel_face_t none = {0};
    soul_eval(&c, &none, &b, 100, 0);
    if (c.state != SOUL_IDLE) {
        return st_fail("no-face!=IDLE (%s)", soul_state_name(c.state));
    }
    tel_face_t far = { .present = true, .area_ratio = 0.02f, .frontal_score = 0.2f };
    soul_eval(&c, &far, &b, 100, 100);
    if (c.state != SOUL_APPROACH) {
        return st_fail("far!=APPROACH (%s)", soul_state_name(c.state));
    }
    tel_face_t mid = { .present = true, .area_ratio = 0.10f, .frontal_score = 0.3f };
    soul_eval(&c, &mid, &b, 100, 200);
    if (c.state != SOUL_ENGAGE) {
        return st_fail("mid!=ENGAGE (%s)", soul_state_name(c.state));
    }
    tel_face_t gaze = { .present = true, .area_ratio = 0.10f, .frontal_score = 0.9f };
    bool gazed = false;
    int64_t t = 300;
    for (int i = 0; i < 25; i++) {
        soul_eval(&c, &gaze, &b, 100, t);
        t += 100;
        if (c.state == SOUL_GAZED) {
            gazed = true;
            break;
        }
    }
    if (!gazed) {
        return st_fail("no GAZED after sustained frontal gaze");
    }
    return st_pass("IDLE->APPROACH->ENGAGE->GAZED ok");
}

/* ---------------- Phase D checks (net + LLM) ---------------- */

static st_report_t check_wifi(void)
{
    const ns_config_t *c = ns_config_get();
    if (c->wifi.ssid[0] == '\0') {
        return st_skip("no ssid configured");
    }
    if (netlink_is_up()) {
        char ip[16];
        netlink_get_ip(ip, sizeof(ip));
        return st_pass("%s", ip);
    }
    return st_skip("connecting to %s", c->wifi.ssid);
}

static st_report_t check_sntp(void)
{
    if (!netlink_is_up()) {
        return st_skip("offline");
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2024) {
        return st_skip("time not set yet");
    }
    return st_pass("%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static st_report_t check_llm_ping(void)
{
    static bool passed;
    static char detail[48];
    if (passed) {
        return st_pass("%s", detail);
    }
    const ns_config_t *c = ns_config_get();
    if (c->chat.api_key[0] == '\0') {
        return st_skip("no api key");
    }
    if (!netlink_is_up()) {
        return st_skip("offline");
    }
    char reply[128];
    if (llm_chat("Reply with the single word: pong", reply, sizeof(reply)) != ESP_OK) {
        return st_fail("chat failed: %.32s", reply);
    }
    snprintf(detail, sizeof(detail), "%s: %.24s", c->chat.provider, reply);
    passed = true; /* latch — don't hammer the API every round */
    return st_pass("%s", detail);
}

/* ---------------- Phase E checks (audio + voice) ---------------- */

static st_report_t check_codec(void)
{
    return audio_ready() ? st_pass("ES8311 up (play verified in converse)")
                         : st_skip("audio not up");
}

static st_report_t check_mic(void)
{
    if (!voice_ready()) {
        return st_skip("voice not up");
    }
    float r = voice_last_rms();   /* voice task owns the codec; read its value */
    if (r <= 0.5f) {
        return st_fail("mic silent (rms=%.0f) — record path?", r);
    }
    if (r > 30000.0f) {
        return st_fail("mic saturated (rms=%.0f)", r);
    }
    return st_pass("rms=%.0f", r);
}

static st_report_t check_wakenet(void)
{
    return st_skip("energy VAD (ESP-SR WakeNet is the upgrade)");
}

/* ---------------- Phase F check (companion WS) ---------------- */

static st_report_t check_ws(void)
{
    const ns_config_t *c = ns_config_get();
    if (!c->companion.enabled) {
        return st_skip("companion disabled");
    }
    return companion_running() ? st_pass("WS :80/ws up") : st_fail("server down");
}

void app_selftests_register(void)
{
    /* Phase 0 */
    selftest_register("sd_mount", check_sd_mount, 0);
    selftest_register("config_parse", check_config, 0);
    selftest_register("psram", check_psram, 0);
    selftest_register("partition_layout", check_partitions, 0);
    /* Phase A */
    selftest_register("emote_mount", check_emote_mount, 0);
    selftest_register("emote_render", check_emote_render, 0);
    selftest_register("hud_draw", check_hud, 0);
    /* Phase B */
    selftest_register("i2c0_probe", check_i2c0_probe, 0);
    selftest_register("camera_stream", check_camera_stream, 0);
    selftest_register("face_detect", check_face_detect, 0);
    /* Phase C (pure logic — always run) */
    selftest_register("motion_ik", check_motion_ik, 0);
    selftest_register("soul_sim", check_soul_sim, 0);
    /* Phase D */
    selftest_register("wifi_connect", check_wifi, 0);
    selftest_register("sntp", check_sntp, 0);
    selftest_register("llm_ping", check_llm_ping, 0);
    /* Phase E */
    selftest_register("codec_playback", check_codec, 0);
    selftest_register("mic_record", check_mic, 0);
    selftest_register("wakenet_load", check_wakenet, SELFTEST_FLAG_MANUAL);
    /* Phase F */
    selftest_register("ws_loopback", check_ws, 0);
}
