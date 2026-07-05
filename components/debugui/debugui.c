#include "debugui.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "camera.h"
#include "face.h"
#include "gfx.h"
#include "telemetry.h"
#include "touch.h"

static const char *TAG = "debugui";

extern const lv_font_t font_puhui_basic_20_4;

#define POLL_MS          20
#define TAP_DEBOUNCE_MS  40
#define TAP_GAP_MS       1000
#define TAP_TARGET       10

/* Fixed raw(FT6x36)->canvas map: canvas is 640x480 landscape, flush rotates it
 * 90° CW onto the 480x640 panel. Composed with the panel's touch orientation
 * (testP4-verified swap=0/mx=1/my=1) this is exact — no calibration. */
#define CANVAS_W 640
#define CANVAS_H 480
static inline void raw_to_canvas(const touch_point_t *r, int *cx, int *cy)
{
    int x = CANVAS_W - 1 - r->y;
    int y = r->x;
    *cx = x < 0 ? 0 : (x >= CANVAS_W ? CANVAS_W - 1 : x);
    *cy = y < 0 ? 0 : (y >= CANVAS_H ? CANVAS_H - 1 : y);
}

/* ------------------------------ pages ------------------------------ */
enum { PG_FACE, PG_MENU, PG_CAMERA };
static int          s_page = PG_FACE;
static gfx_handle_t s_gfx;

/* menu */
enum { ACT_CAMERA, ACT_WIFI, ACT_CLOSE, N_BTN };
static const char *s_btn_txt[N_BTN] = { "Camera preview", "WiFi setup", "Close menu" };
#define BTN_W 340
#define BTN_H 54
#define BTN_X ((CANVAS_W - BTN_W) / 2)
#define BTN_GAP 66
#define BTN_Y0 120
static inline int btn_y(int i) { return BTN_Y0 + i * BTN_GAP; }
static gfx_obj_t *s_title;
static gfx_obj_t *s_btn[N_BTN];

/* camera page */
#define CAM_W CAMERA_DET_W          /* 300 */
#define CAM_H CAMERA_DET_H          /* 480 */
#define CAM_BACK_X 330
#define CAM_BACK_Y 410
#define CAM_BACK_W 300
#define CAM_BACK_H 56
static gfx_obj_t        *s_cam_img;
static gfx_obj_t        *s_cam_stats;
static gfx_obj_t        *s_cam_back;
static uint16_t         *s_cam_buf;
static gfx_image_dsc_t   s_cam_dsc;

/* ------------------------------ helpers ------------------------------ */
static gfx_obj_t *make_label(gfx_disp_t *disp, int x, int y, int w, int h,
                             uint32_t txt, uint32_t bg, gfx_text_align_t align, const char *s)
{
    gfx_obj_t *o = gfx_label_create(disp);
    if (!o) {
        return NULL;
    }
    gfx_label_set_font(o, (void *)&font_puhui_basic_20_4);
    gfx_label_set_color(o, GFX_COLOR_HEX(txt));
    gfx_label_set_bg_enable(o, true);
    gfx_label_set_bg_color(o, GFX_COLOR_HEX(bg));
    gfx_label_set_text_align(o, align);
    gfx_label_set_long_mode(o, GFX_LABEL_LONG_CLIP);
    gfx_obj_set_size(o, w, h);
    gfx_obj_set_pos(o, x, y);
    gfx_label_set_text(o, s);
    gfx_obj_set_visible(o, false);
    return o;
}

static void show_page(int pg)
{
    if (!s_gfx || gfx_emote_lock(s_gfx) != ESP_OK) {
        return;
    }
    bool menu = (pg == PG_MENU), cam = (pg == PG_CAMERA);
    if (s_title) gfx_obj_set_visible(s_title, menu);
    for (int i = 0; i < N_BTN; i++) if (s_btn[i]) gfx_obj_set_visible(s_btn[i], menu);
    if (s_cam_img)   gfx_obj_set_visible(s_cam_img, cam);
    if (s_cam_stats) gfx_obj_set_visible(s_cam_stats, cam);
    if (s_cam_back)  gfx_obj_set_visible(s_cam_back, cam);
    gfx_emote_unlock(s_gfx);
    s_page = pg;
    /* dim the whole face under the menu/camera pages; restore on the face page */
    if (pg == PG_FACE) {
        face_clear_veil();   /* hud re-arms its own veil on next toggle; fine for debug */
    }
}

static void cam_refresh(void)
{
    if (!s_cam_img || !s_cam_buf) {
        return;
    }
    camera_copy_latest(s_cam_buf, (size_t)CAM_W * CAM_H);

    tel_snapshot_t t;
    telemetry_get(&t);
    char st[48];
    snprintf(st, sizeof(st), "cam#%u p%d a%.3f fr%.2f %.0ffps",
             (unsigned)camera_frame_count(), t.face.present,
             t.face.area_ratio, t.face.frontal_score, t.fps_detect);

    if (gfx_emote_lock(s_gfx) != ESP_OK) {
        return;
    }
    gfx_img_set_src(s_cam_img, &s_cam_dsc);   /* re-set to mark dirty -> re-render */
    if (s_cam_stats) gfx_label_set_text(s_cam_stats, st);
    gfx_emote_unlock(s_gfx);
}

static bool in_rect(int cx, int cy, int x, int y, int w, int h)
{
    return cx >= x && cx < x + w && cy >= y && cy < y + h;
}

/* ------------------------------ task ------------------------------ */
static void ui_task(void *arg)
{
    (void)arg;
    bool     was = false;
    int      count = 0;
    int64_t  last_tap_us = -10000000;
    unsigned tick = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        /* live camera preview refresh (~every 60ms while on the camera page) */
        if (s_page == PG_CAMERA && (++tick % 3) == 0) {
            cam_refresh();
        }

        touch_point_t pt;
        bool pressed;
        if (touch_read_raw(&pt, &pressed) != ESP_OK) {
            was = false;
            continue;
        }
        if (!(pressed && !was)) {
            was = pressed;
            continue;
        }
        int64_t now = esp_timer_get_time();
        int64_t gap_ms = (now - last_tap_us) / 1000;
        was = pressed;
        if (gap_ms < TAP_DEBOUNCE_MS) {
            continue;
        }
        last_tap_us = now;

        int cx, cy;
        raw_to_canvas(&pt, &cx, &cy);
        ESP_LOGI(TAG, "tap raw(%u,%u) canvas(%d,%d) pg%d", pt.x, pt.y, cx, cy, s_page);

        if (s_page == PG_MENU) {
            for (int i = 0; i < N_BTN; i++) {
                if (in_rect(cx, cy, BTN_X, btn_y(i), BTN_W, BTN_H)) {
                    if (i == ACT_CAMERA) {
                        ESP_LOGI(TAG, "-> camera page");
                        show_page(PG_CAMERA);
                    } else if (i == ACT_WIFI) {
                        ESP_LOGI(TAG, "wifi page (pending)");
                        face_set_tip("wifi page next");
                    } else {
                        show_page(PG_FACE);
                        face_set_tip("");
                    }
                    break;
                }
            }
            count = 0;
            continue;
        }
        if (s_page == PG_CAMERA) {
            if (in_rect(cx, cy, CAM_BACK_X, CAM_BACK_Y, CAM_BACK_W, CAM_BACK_H)) {
                show_page(PG_MENU);
            }
            count = 0;
            continue;
        }

        /* PG_FACE: count rapid taps -> open menu */
        count = (gap_ms <= TAP_GAP_MS) ? count + 1 : 1;
        ESP_LOGI(TAG, "face tap #%d/%d (gap %lldms)", count, TAP_TARGET, (long long)gap_ms);
        char tip[24];
        if (count >= TAP_TARGET) {
            ESP_LOGI(TAG, "*** 10-tap -> menu ***");
            show_page(PG_MENU);
            face_set_tip("");
            count = 0;
        } else {
            snprintf(tip, sizeof(tip), "tap %d/%d", count, TAP_TARGET);
            face_set_tip(tip);
        }
    }
}

esp_err_t debugui_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = touch_init(bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed (%s) — debug UI disabled", esp_err_to_name(err));
        return err;
    }

    s_cam_buf = heap_caps_aligned_calloc(128, (size_t)CAM_W * CAM_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_cam_buf) {
        s_cam_dsc.header.magic  = C_ARRAY_HEADER_MAGIC;
        s_cam_dsc.header.cf     = GFX_COLOR_FORMAT_RGB565;
        s_cam_dsc.header.w      = CAM_W;
        s_cam_dsc.header.h      = CAM_H;
        s_cam_dsc.header.stride = CAM_W * sizeof(uint16_t);
        s_cam_dsc.data          = (const uint8_t *)s_cam_buf;
        s_cam_dsc.data_size     = (size_t)CAM_W * CAM_H * sizeof(uint16_t);
    }

    gfx_disp_t *disp = face_gfx_disp();
    s_gfx = face_gfx_handle();
    if (disp && s_gfx && gfx_emote_lock(s_gfx) == ESP_OK) {
        /* menu widgets */
        s_title = make_label(disp, 120, 50, 400, 30, 0xFFFFFF, 0x101820,
                             GFX_TEXT_ALIGN_CENTER, "== DEBUG MENU ==");
        for (int i = 0; i < N_BTN; i++) {
            uint32_t bg = (i == ACT_CLOSE) ? 0x402020 : 0x203040;
            s_btn[i] = make_label(disp, BTN_X, btn_y(i), BTN_W, BTN_H, 0x60FFC0, bg,
                                  GFX_TEXT_ALIGN_CENTER, s_btn_txt[i]);
        }
        /* camera page widgets */
        if (s_cam_buf) {
            s_cam_img = gfx_img_create(disp);
            if (s_cam_img) {
                gfx_img_set_src(s_cam_img, &s_cam_dsc);
                gfx_obj_set_pos(s_cam_img, 0, 0);
                gfx_obj_set_visible(s_cam_img, false);
            }
        }
        s_cam_stats = make_label(disp, 306, 12, 330, 28, 0x60FFC0, 0x101820,
                                 GFX_TEXT_ALIGN_LEFT, "cam ...");
        s_cam_back = make_label(disp, CAM_BACK_X, CAM_BACK_Y, CAM_BACK_W, CAM_BACK_H,
                                0xFFFFFF, 0x203040, GFX_TEXT_ALIGN_CENTER, "< Back");
        gfx_emote_unlock(s_gfx);
    }
    return ESP_OK;
}

esp_err_t debugui_start(void)
{
    if (!touch_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreatePinnedToCore(ui_task, "debugui", 4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "debug UI up: tap the screen 10x fast to open the menu");
    return ESP_OK;
}

bool debugui_touch_ok(void)
{
    return touch_ready();
}
