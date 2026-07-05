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

#define HUD_LINES   6
#define HUD_X       4
#define HUD_Y0      2
#define HUD_DY      22

static gfx_obj_t       *s_labels[HUD_LINES];
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
    for (int i = 0; i < HUD_LINES; i++) {
        s_labels[i] = gfx_label_create(disp);
        if (!s_labels[i]) {
            gfx_emote_unlock(s_gfx);
            return ESP_FAIL;
        }
        gfx_label_set_font(s_labels[i], (void *)&font_puhui_basic_20_4);
        gfx_label_set_color(s_labels[i], GFX_COLOR_HEX(0x30FF60));
        gfx_obj_set_pos(s_labels[i], HUD_X, HUD_Y0 + i * HUD_DY);
        gfx_label_set_text(s_labels[i], "");
    }
    gfx_emote_unlock(s_gfx);

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

        snprintf(line[0], sizeof(line[0]), "SOUL %s  EMO %s",
                 soul_state_name(t.soul), t.emotion);
        snprintf(line[1], sizeof(line[1]), "FACE p%d x%+.2f y%+.2f a%.3f f%.2f",
                 t.face.present, t.face.cx, t.face.cy, t.face.area_ratio, t.face.frontal_score);
        snprintf(line[2], sizeof(line[2]), "MOT v(%.2f,%.2f,%.2f) d[%d,%d,%d]%s",
                 t.motion.vx, t.motion.vy, t.motion.wz,
                 t.motion.duty[0], t.motion.duty[1], t.motion.duty[2],
                 t.motion.enabled ? "" : " off");
        snprintf(line[3], sizeof(line[3]), "ENC[%d,%d,%d] CUR %s",
                 (int)t.enc.rpm[0], (int)t.enc.rpm[1], (int)t.enc.rpm[2],
                 t.current_present ? "" : "--");
        if (t.current_present) {
            snprintf(line[3] + strlen(line[3]), sizeof(line[3]) - strlen(line[3]),
                     "%dmA", (int)(t.current_a * 1000));
        }
        snprintf(line[4], sizeof(line[4]), "NET %s LLM %s VOI %s",
                 t.net_up ? t.ip : "off", t.llm, t.voice);
        snprintf(line[5], sizeof(line[5]), "FPS r%.0f/d%.0f HEAP %uk PSRAM %uM",
                 render_fps, t.fps_detect, (unsigned)(t.free_heap / 1024),
                 (unsigned)(t.free_psram / (1024 * 1024)));

        if (gfx_emote_lock(s_gfx) != ESP_OK) {
            continue;
        }
        for (int i = 0; i < HUD_LINES; i++) {
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
        for (int i = 0; i < HUD_LINES; i++) {
            if (s_labels[i]) {
                gfx_obj_set_visible(s_labels[i], on);
            }
        }
        gfx_emote_unlock(s_gfx);
    }
}
