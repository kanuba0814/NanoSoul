#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "face.h"
#include "gfx.h"
#include "telemetry.h"

static const char *TAG = "hud";

/* Reuse the PuHui font the emote player already links (has ASCII glyphs). */
extern const lv_font_t font_puhui_basic_20_4;

#define HUD_LINES   10
#define HUD_X       6
#define HUD_Y0      4
#define HUD_DY      26
#define HUD_W       288   /* narrow corner panel, not a full-width band */
#define HUD_H       26    /* MUST be >= font line_height (25) or the label
                           * renders zero glyphs (draw loop breaks on line 0). */
/* Nominal wheel RPM at full duty (1023), for the computed target-speed readout.
 * Rough gearmotor estimate; refine once a wheel is actually driven + measured. */
#define HUD_WHEEL_RPM_FULL 200

static gfx_obj_t       *s_labels[HUD_LINES];
static int              s_made;   /* labels actually created (<= HUD_LINES) */
static gfx_handle_t     s_gfx;
static volatile bool    s_enabled = true;
static volatile bool    s_running;
static uint32_t         s_last_frames;
static int64_t          s_last_us;

esp_err_t hud_init(void)
{
    gfx_disp_t *disp = face_gfx_disp();
    s_gfx = face_gfx_handle();
    if (!disp || !s_gfx) {
        ESP_LOGE(TAG, "face gfx not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (gfx_emote_lock(s_gfx) != ESP_OK) {
        return ESP_FAIL;
    }
    int made = 0;
    for (int i = 0; i < HUD_LINES; i++) {
        s_labels[i] = gfx_label_create(disp);
        if (!s_labels[i]) {
            /* Don't kill the whole overlay if the gfx object pool runs out —
             * show as many lines as we could make and shout about the shortfall. */
            ESP_LOGE(TAG, "gfx_label_create failed at line %d/%d", i, HUD_LINES);
            break;
        }
        gfx_label_set_font(s_labels[i], (void *)&font_puhui_basic_20_4);
        gfx_label_set_color(s_labels[i], GFX_COLOR_HEX(0x40FF70));
        /* No solid bg: the face module paints a translucent gray veil under this
         * rect during flush, so the emote stays visible through the panel. */
        /* Match the emote player's own (working) tip_label: single-line CLIP, not
         * the default WRAP — a WRAP label one line tall renders no glyphs when the
         * text is wider than the box. Left-align + explicit visible. */
        gfx_label_set_text_align(s_labels[i], GFX_TEXT_ALIGN_LEFT);
        gfx_label_set_long_mode(s_labels[i], GFX_LABEL_LONG_CLIP);
        /* geometry MUST be >= font line_height (25) or the label renders zero
         * glyphs (draw loop breaks on line 0). */
        gfx_obj_set_size(s_labels[i], HUD_W, HUD_H);
        gfx_obj_set_pos(s_labels[i], HUD_X, HUD_Y0 + i * HUD_DY);
        gfx_label_set_text(s_labels[i], "");
        gfx_obj_set_visible(s_labels[i], true);
        made++;
    }
    gfx_emote_unlock(s_gfx);

    s_made = made;
    if (made == 0) {
        ESP_LOGE(TAG, "no HUD labels created — overlay will be blank");
        return ESP_FAIL;
    }
    /* Translucent backing under the whole panel (a little padding around text). */
    face_set_veil_rect(HUD_X - 4, HUD_Y0 - 2, HUD_W + 6, made * HUD_DY + 4);
    ESP_LOGI(TAG, "HUD up: %d/%d lines, corner @(%d,%d) %dx%d px",
             made, HUD_LINES, HUD_X, HUD_Y0, HUD_W, HUD_H);

    s_last_frames = face_frame_count();
    s_last_us = esp_timer_get_time();
    return ESP_OK;
}

static void hud_task(void *arg)
{
    (void)arg;
    char line[HUD_LINES][80];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_enabled) {
            continue;
        }

        telemetry_refresh_perf();
        tel_snapshot_t t;
        telemetry_get(&t);

        int64_t now = esp_timer_get_time();
        uint32_t frames = face_frame_count();
        float dt = (now - s_last_us) / 1e6f;
        float render_fps = dt > 0 ? (frames - s_last_frames) / dt : 0.0f;
        s_last_frames = frames;
        s_last_us = now;
        telemetry_set_fps(render_fps, t.fps_detect);

        /* Translucent corner panel, grouped as PERCEIVE -> DECIDE(expected) ->
         * ACT(actual) so the expected decision output sits right next to what the
         * hardware actually did. rpm* = computed target speed from the IK duty
         * (motors unwired => 'act rpm' stays 0, but the target still shows). */
        int tgt_rpm[3];
        for (int k = 0; k < 3; k++) {
            tgt_rpm[k] = t.motion.duty[k] * HUD_WHEEL_RPM_FULL / 1023;
        }
        snprintf(line[0], sizeof(line[0]), "NanoSoul %s  %s",
                 soul_state_name(t.soul), t.emotion);
        snprintf(line[1], sizeof(line[1]), "fps r%.0f d%.0f  mem %uk",
                 render_fps, t.fps_detect, (unsigned)(t.free_heap / 1024));
        /* --- perceive (actual sensor) --- */
        snprintf(line[2], sizeof(line[2]), "SEE cam p%d x%+.2f y%+.2f",
                 t.face.present, t.face.cx, t.face.cy);
        snprintf(line[3], sizeof(line[3]), "    dist a%.3f front fr%.2f",
                 t.face.area_ratio, t.face.frontal_score);
        /* --- decide (expected / computed output) --- */
        snprintf(line[4], sizeof(line[4]), "WANT v%+.2f %+.2f w%+.2f",
                 t.motion.vx, t.motion.vy, t.motion.wz);
        snprintf(line[5], sizeof(line[5]), "    rpm* %d/%d/%d %s",
                 tgt_rpm[0], tgt_rpm[1], tgt_rpm[2], t.motion.enabled ? "" : "(calc)");
        /* --- act (actual hardware) --- */
        snprintf(line[6], sizeof(line[6]), "GOT rpm %d/%d/%d",
                 (int)t.enc.rpm[0], (int)t.enc.rpm[1], (int)t.enc.rpm[2]);
        if (t.current_present) {
            snprintf(line[7], sizeof(line[7]), "    duty %d/%d/%d %dmA",
                     t.motion.duty[0], t.motion.duty[1], t.motion.duty[2],
                     (int)(t.current_a * 1000));
        } else {
            snprintf(line[7], sizeof(line[7]), "    duty %d/%d/%d cur--",
                     t.motion.duty[0], t.motion.duty[1], t.motion.duty[2]);
        }
        /* --- links --- */
        char pcbuf[28] = "";
        if (t.pc.activity[0]) {
            snprintf(pcbuf, sizeof(pcbuf), " PC:%.4s/%s%s", t.pc.focus,
                     t.pc.activity, t.pc.dnd ? "/dnd" : "");
        }
        if (t.net_up) {
            snprintf(line[8], sizeof(line[8]), "NET %s %ddB%s", t.ip, t.rssi, pcbuf);
        } else {
            snprintf(line[8], sizeof(line[8]), "NET off%s", pcbuf);
        }
        snprintf(line[9], sizeof(line[9]), "llm %s  voi %s  E%.1f S%.1f",
                 t.llm, t.voice, t.mood_energy, t.mood_social);

        if (gfx_emote_lock(s_gfx) != ESP_OK) {
            continue;
        }
        for (int i = 0; i < s_made; i++) {
            gfx_label_set_text(s_labels[i], line[i]);
        }
        gfx_emote_unlock(s_gfx);
    }
}

esp_err_t hud_start(void)
{
    if (!s_gfx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreatePinnedToCore(hud_task, "hud", 4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }
    s_running = true;
    return ESP_OK;
}

bool hud_ready(void)
{
    return s_running;
}

void hud_set_enabled(bool on)
{
    if (on == s_enabled || !s_gfx) {
        s_enabled = on;
        return;
    }
    s_enabled = on;
    if (gfx_emote_lock(s_gfx) == ESP_OK) {
        for (int i = 0; i < s_made; i++) {
            if (s_labels[i]) {
                gfx_obj_set_visible(s_labels[i], on);
            }
        }
        gfx_emote_unlock(s_gfx);
    }
    /* Keep the translucent veil in lock-step with the labels. */
    if (on) {
        face_set_veil_rect(HUD_X - 4, HUD_Y0 - 2, HUD_W + 6, s_made * HUD_DY + 4);
    } else {
        face_clear_veil();
    }
}
