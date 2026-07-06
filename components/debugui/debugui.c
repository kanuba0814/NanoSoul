#include "debugui.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "camera.h"
#include "face.h"
#include "gfx.h"
#include "netlink.h"
#include "telemetry.h"
#include "touch.h"

static const char *TAG = "debugui";

extern const lv_font_t font_puhui_basic_20_4;

#define POLL_MS          20
#define TAP_DEBOUNCE_MS  40
#define TAP_GAP_MS       1000
#define TAP_TARGET       10

/* Fixed raw(FT6x36)->canvas map: canvas is 640x480 landscape, flush rotates it
 * 90° CW onto the 480x640 panel; composed with the panel's touch orientation
 * this is exact — no calibration. */
#define CANVAS_W 640
#define CANVAS_H 480
static inline void raw_to_canvas(const touch_point_t *r, int *cx, int *cy)
{
    int x = CANVAS_W - 1 - r->y;
    int y = r->x;
    *cx = x < 0 ? 0 : (x >= CANVAS_W ? CANVAS_W - 1 : x);
    *cy = y < 0 ? 0 : (y >= CANVAS_H ? CANVAS_H - 1 : y);
}

enum { PG_FACE, PG_MENU, PG_CAMERA, PG_WIFI, PG_KEYBOARD };
static int          s_page = PG_FACE;
static gfx_handle_t s_gfx;
static gfx_disp_t  *s_disp;

/* ---- menu ---- */
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

/* ---- camera page ---- */
#define CAM_W CAMERA_DET_W
#define CAM_H CAMERA_DET_H
#define CAM_BACK_X 330
#define CAM_BACK_Y 410
#define CAM_BACK_W 300
#define CAM_BACK_H 56
static gfx_obj_t        *s_cam_img;
static gfx_obj_t        *s_cam_stats;
static gfx_obj_t        *s_cam_back;
static uint16_t         *s_cam_buf;
static gfx_image_dsc_t   s_cam_dsc;

/* ---- wifi list page ---- */
#define WIFI_ROWS 6
#define WROW_X 16
#define WROW_W 608
#define WROW_H 48
#define WROW_Y0 88
#define WROW_PITCH 52
static gfx_obj_t   *s_wifi_title, *s_wifi_status, *s_wifi_rescan, *s_wifi_back;
static gfx_obj_t   *s_wifi_row[WIFI_ROWS];
static netlink_ap_t s_aps[WIFI_ROWS];
static int          s_ap_count;
static volatile bool s_scan_busy, s_scan_done;

/* ---- keyboard page ---- */
enum { K_CHAR, K_SHIFT, K_DEL, K_SPACE, K_CONNECT, K_BACK };
typedef struct { gfx_obj_t *obj; int x, y, w, h; char lc, uc; int act; } kbd_key_t;
static kbd_key_t s_keys[64];
static int       s_nkeys;
static gfx_obj_t *s_kb_ssid, *s_kb_pass;
static char       s_sel_ssid[33];
static char       s_pass[65];
static bool       s_shift;

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

static bool in_rect(int cx, int cy, int x, int y, int w, int h)
{
    return cx >= x && cx < x + w && cy >= y && cy < y + h;
}

static void show_page(int pg)
{
    if (!s_gfx || gfx_emote_lock(s_gfx) != ESP_OK) {
        return;
    }
    bool menu = (pg == PG_MENU), cam = (pg == PG_CAMERA);
    bool wifi = (pg == PG_WIFI), kb = (pg == PG_KEYBOARD);
    if (s_title) gfx_obj_set_visible(s_title, menu);
    for (int i = 0; i < N_BTN; i++) if (s_btn[i]) gfx_obj_set_visible(s_btn[i], menu);
    if (s_cam_img)   gfx_obj_set_visible(s_cam_img, cam);
    if (s_cam_stats) gfx_obj_set_visible(s_cam_stats, cam);
    if (s_cam_back)  gfx_obj_set_visible(s_cam_back, cam);
    if (s_wifi_title)  gfx_obj_set_visible(s_wifi_title, wifi);
    if (s_wifi_status) gfx_obj_set_visible(s_wifi_status, wifi);
    if (s_wifi_rescan) gfx_obj_set_visible(s_wifi_rescan, wifi);
    if (s_wifi_back)   gfx_obj_set_visible(s_wifi_back, wifi);
    for (int i = 0; i < WIFI_ROWS; i++) {
        if (s_wifi_row[i]) gfx_obj_set_visible(s_wifi_row[i], wifi && i < s_ap_count);
    }
    if (s_kb_ssid) gfx_obj_set_visible(s_kb_ssid, kb);
    if (s_kb_pass) gfx_obj_set_visible(s_kb_pass, kb);
    for (int i = 0; i < s_nkeys; i++) {
        if (s_keys[i].obj) gfx_obj_set_visible(s_keys[i].obj, kb);
    }
    gfx_emote_unlock(s_gfx);
    s_page = pg;
    if (pg == PG_FACE) {
        face_clear_veil();
    }
}

/* ---- wifi scan ---- */
static void set_text_locked(gfx_obj_t *o, const char *s)
{
    if (o && gfx_emote_lock(s_gfx) == ESP_OK) {
        gfx_label_set_text(o, s);
        gfx_emote_unlock(s_gfx);
    }
}

static void scan_task(void *arg)
{
    (void)arg;
    int n = 0;
    netlink_scan(s_aps, WIFI_ROWS, &n);
    s_ap_count = n;
    s_scan_done = true;
    s_scan_busy = false;
    vTaskDelete(NULL);
}

static void start_scan(void)
{
    if (s_scan_busy) {
        return;
    }
    s_scan_busy = true;
    s_scan_done = false;
    s_ap_count = 0;
    set_text_locked(s_wifi_status, "scanning...");
    if (gfx_emote_lock(s_gfx) == ESP_OK) {
        for (int i = 0; i < WIFI_ROWS; i++) if (s_wifi_row[i]) gfx_obj_set_visible(s_wifi_row[i], false);
        gfx_emote_unlock(s_gfx);
    }
    xTaskCreatePinnedToCore(scan_task, "wifiscan", 4096, NULL, 4, NULL, 0);
}

static void wifi_populate(void)
{
    if (gfx_emote_lock(s_gfx) != ESP_OK) {
        return;
    }
    for (int i = 0; i < WIFI_ROWS; i++) {
        if (!s_wifi_row[i]) continue;
        if (i < s_ap_count) {
            char r[64];
            snprintf(r, sizeof(r), "%-18.18s %4ddB %s", s_aps[i].ssid, s_aps[i].rssi,
                     s_aps[i].authmode == 0 ? "open" : "lock");
            gfx_label_set_text(s_wifi_row[i], r);
            gfx_obj_set_visible(s_wifi_row[i], s_page == PG_WIFI);
        } else {
            gfx_obj_set_visible(s_wifi_row[i], false);
        }
    }
    gfx_emote_unlock(s_gfx);
    char st[48];
    snprintf(st, sizeof(st), "found %d - tap one (Rescan to retry)", s_ap_count);
    set_text_locked(s_wifi_status, s_ap_count ? st : "no APs - tap Rescan");
}

/* ---- keyboard ---- */
static void kb_update_pass(void)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "pw: %s_", s_pass);
    set_text_locked(s_kb_pass, buf);
}

static void kb_apply_shift(void)
{
    if (gfx_emote_lock(s_gfx) != ESP_OK) {
        return;
    }
    for (int i = 0; i < s_nkeys; i++) {
        if (s_keys[i].act == K_CHAR && s_keys[i].lc != s_keys[i].uc) {
            char c[2] = { s_shift ? s_keys[i].uc : s_keys[i].lc, 0 };
            gfx_label_set_text(s_keys[i].obj, c);
        }
    }
    gfx_emote_unlock(s_gfx);
}

static void kb_open(const char *ssid)
{
    strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));
    s_pass[0] = '\0';
    s_shift = false;
    char t[56];
    snprintf(t, sizeof(t), "AP: %.32s", s_sel_ssid);
    set_text_locked(s_kb_ssid, t);
    kb_update_pass();
    kb_apply_shift();
    show_page(PG_KEYBOARD);
}

static void kb_key(const kbd_key_t *k)
{
    switch (k->act) {
    case K_CHAR: {
        size_t n = strlen(s_pass);
        if (n < sizeof(s_pass) - 1) {
            s_pass[n] = s_shift ? k->uc : k->lc;
            s_pass[n + 1] = '\0';
            kb_update_pass();
        }
        break;
    }
    case K_SHIFT:
        s_shift = !s_shift;
        kb_apply_shift();
        break;
    case K_DEL: {
        size_t n = strlen(s_pass);
        if (n) { s_pass[n - 1] = '\0'; kb_update_pass(); }
        break;
    }
    case K_SPACE: {
        size_t n = strlen(s_pass);
        if (n < sizeof(s_pass) - 1) { s_pass[n] = ' '; s_pass[n + 1] = '\0'; kb_update_pass(); }
        break;
    }
    case K_CONNECT:
        ESP_LOGI(TAG, "connect '%s' (pw %d chars)", s_sel_ssid, (int)strlen(s_pass));
        netlink_connect(s_sel_ssid, s_pass);
        set_text_locked(s_wifi_status, "connecting...");
        show_page(PG_WIFI);
        break;
    case K_BACK:
        show_page(PG_WIFI);
        break;
    }
}

static void add_key(char lc, char uc, int x, int y, int w, int h, int act, const char *label)
{
    if (s_nkeys >= (int)(sizeof(s_keys) / sizeof(s_keys[0]))) {
        return;
    }
    kbd_key_t *k = &s_keys[s_nkeys];
    k->x = x; k->y = y; k->w = w; k->h = h; k->lc = lc; k->uc = uc; k->act = act;
    uint32_t bg = (act == K_CONNECT) ? 0x205020 : (act == K_CHAR ? 0x283848 : 0x384050);
    k->obj = make_label(s_disp, x, y, w, h, 0xFFFFFF, bg, GFX_TEXT_ALIGN_CENTER, label);
    s_nkeys++;
}

#define KW 58
#define KH 46
#define KP 61
static void build_keyboard(void)
{
    static const char *r0 = "1234567890";
    static const char *r1 = "qwertyuiop";
    static const char *r2 = "asdfghjkl";
    static const char *r3 = "zxcvbnm";
    static const char *sym = "@._-#!";
    int y = 92;
    for (int i = 0; r0[i]; i++) add_key(r0[i], r0[i], 8 + i * KP, y, KW, KH, K_CHAR, (char[]){r0[i], 0});
    y += 52;
    for (int i = 0; r1[i]; i++) add_key(r1[i], (char)(r1[i] - 32), 8 + i * KP, y, KW, KH, K_CHAR, (char[]){r1[i], 0});
    y += 52;
    for (int i = 0; r2[i]; i++) add_key(r2[i], (char)(r2[i] - 32), 38 + i * KP, y, KW, KH, K_CHAR, (char[]){r2[i], 0});
    y += 52;
    add_key(0, 0, 8, y, 88, KH, K_SHIFT, "shift");
    for (int i = 0; r3[i]; i++) add_key(r3[i], (char)(r3[i] - 32), 100 + i * KP, y, KW, KH, K_CHAR, (char[]){r3[i], 0});
    add_key(0, 0, 530, y, 102, KH, K_DEL, "del");
    y += 52;
    for (int i = 0; sym[i]; i++) add_key(sym[i], sym[i], 8 + i * KP, y, KW, KH, K_CHAR, (char[]){sym[i], 0});
    add_key(0, 0, 380, y, 252, KH, K_SPACE, "space");
    y += 52;
    add_key(0, 0, 8, y, 300, 52, K_BACK, "< Back");
    add_key(0, 0, 332, y, 300, 52, K_CONNECT, "Connect");
    ESP_LOGI(TAG, "keyboard: %d keys, internal heap %uKB",
             s_nkeys, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

/* ------------------------------ camera refresh ------------------------------ */
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
    gfx_img_set_src(s_cam_img, &s_cam_dsc);
    if (s_cam_stats) gfx_label_set_text(s_cam_stats, st);
    gfx_emote_unlock(s_gfx);
}

static void wifi_status_refresh(void)
{
    tel_snapshot_t t;
    telemetry_get(&t);
    char st[48];
    if (t.net_up) {
        snprintf(st, sizeof(st), "online: %s %ddB", t.ip, t.rssi);
        set_text_locked(s_wifi_status, st);
    }
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
        tick++;

        if (s_page == PG_CAMERA && (tick % 3) == 0) {
            cam_refresh();
        }
        if (s_page == PG_WIFI) {
            if (s_scan_done) { s_scan_done = false; wifi_populate(); }
            if ((tick % 25) == 0) wifi_status_refresh();
        }

        touch_point_t pt;
        bool pressed;
        if (touch_read_raw(&pt, &pressed) != ESP_OK) { was = false; continue; }
        if (!(pressed && !was)) { was = pressed; continue; }
        int64_t now = esp_timer_get_time();
        int64_t gap_ms = (now - last_tap_us) / 1000;
        was = pressed;
        if (gap_ms < TAP_DEBOUNCE_MS) continue;
        last_tap_us = now;

        int cx, cy;
        raw_to_canvas(&pt, &cx, &cy);

        if (s_page == PG_MENU) {
            for (int i = 0; i < N_BTN; i++) {
                if (in_rect(cx, cy, BTN_X, btn_y(i), BTN_W, BTN_H)) {
                    if (i == ACT_CAMERA)      show_page(PG_CAMERA);
                    else if (i == ACT_WIFI)   { show_page(PG_WIFI); start_scan(); }
                    else                      { show_page(PG_FACE); face_set_tip(""); }
                    break;
                }
            }
            count = 0; continue;
        }
        if (s_page == PG_CAMERA) {
            if (in_rect(cx, cy, CAM_BACK_X, CAM_BACK_Y, CAM_BACK_W, CAM_BACK_H)) show_page(PG_MENU);
            count = 0; continue;
        }
        if (s_page == PG_WIFI) {
            if (in_rect(cx, cy, 460, 410, 172, 52)) { start_scan(); }
            else if (in_rect(cx, cy, 16, 410, 160, 52)) { show_page(PG_MENU); }
            else {
                for (int i = 0; i < WIFI_ROWS && i < s_ap_count; i++) {
                    if (in_rect(cx, cy, WROW_X, WROW_Y0 + i * WROW_PITCH, WROW_W, WROW_H)) {
                        if (s_aps[i].authmode == 0) {   // open AP: connect directly
                            netlink_connect(s_aps[i].ssid, "");
                            set_text_locked(s_wifi_status, "connecting...");
                        } else {
                            kb_open(s_aps[i].ssid);
                        }
                        break;
                    }
                }
            }
            count = 0; continue;
        }
        if (s_page == PG_KEYBOARD) {
            for (int i = 0; i < s_nkeys; i++) {
                if (in_rect(cx, cy, s_keys[i].x, s_keys[i].y, s_keys[i].w, s_keys[i].h)) {
                    kb_key(&s_keys[i]);
                    break;
                }
            }
            count = 0; continue;
        }

        /* PG_FACE: rapid taps -> menu */
        count = (gap_ms <= TAP_GAP_MS) ? count + 1 : 1;
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

    s_disp = face_gfx_disp();
    s_gfx = face_gfx_handle();
    if (!s_disp || !s_gfx || gfx_emote_lock(s_gfx) != ESP_OK) {
        return ESP_OK;
    }
    /* menu */
    s_title = make_label(s_disp, 120, 50, 400, 30, 0xFFFFFF, 0x101820, GFX_TEXT_ALIGN_CENTER, "== DEBUG MENU ==");
    for (int i = 0; i < N_BTN; i++) {
        uint32_t bg = (i == ACT_CLOSE) ? 0x402020 : 0x203040;
        s_btn[i] = make_label(s_disp, BTN_X, btn_y(i), BTN_W, BTN_H, 0x60FFC0, bg, GFX_TEXT_ALIGN_CENTER, s_btn_txt[i]);
    }
    /* camera page */
    if (s_cam_buf) {
        s_cam_img = gfx_img_create(s_disp);
        if (s_cam_img) {
            gfx_img_set_src(s_cam_img, &s_cam_dsc);
            gfx_obj_set_pos(s_cam_img, 0, 0);
            gfx_obj_set_visible(s_cam_img, false);
        }
    }
    s_cam_stats = make_label(s_disp, 306, 12, 330, 28, 0x60FFC0, 0x101820, GFX_TEXT_ALIGN_LEFT, "cam ...");
    s_cam_back = make_label(s_disp, CAM_BACK_X, CAM_BACK_Y, CAM_BACK_W, CAM_BACK_H, 0xFFFFFF, 0x203040, GFX_TEXT_ALIGN_CENTER, "< Back");
    /* wifi list page */
    s_wifi_title = make_label(s_disp, 16, 12, 608, 28, 0xFFFFFF, 0x101820, GFX_TEXT_ALIGN_LEFT, "WiFi - tap an AP");
    s_wifi_status = make_label(s_disp, 16, 46, 608, 28, 0x60FFC0, 0x101820, GFX_TEXT_ALIGN_LEFT, "");
    for (int i = 0; i < WIFI_ROWS; i++) {
        s_wifi_row[i] = make_label(s_disp, WROW_X, WROW_Y0 + i * WROW_PITCH, WROW_W, WROW_H, 0xFFFFFF, 0x283848, GFX_TEXT_ALIGN_LEFT, "");
    }
    s_wifi_rescan = make_label(s_disp, 460, 410, 172, 52, 0xFFFFFF, 0x205020, GFX_TEXT_ALIGN_CENTER, "Rescan");
    s_wifi_back = make_label(s_disp, 16, 410, 160, 52, 0xFFFFFF, 0x402020, GFX_TEXT_ALIGN_CENTER, "< Back");
    /* keyboard page */
    s_kb_ssid = make_label(s_disp, 8, 12, 624, 28, 0xFFFFFF, 0x101820, GFX_TEXT_ALIGN_LEFT, "AP:");
    s_kb_pass = make_label(s_disp, 8, 50, 624, 34, 0x60FFC0, 0x101820, GFX_TEXT_ALIGN_LEFT, "pw: _");
    build_keyboard();
    gfx_emote_unlock(s_gfx);
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
