#include "app_selftests.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"

#include "face.h"
#include "hud.h"
#include "ns_config.h"
#include "sd_storage.h"
#include "selftest.h"

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
}
