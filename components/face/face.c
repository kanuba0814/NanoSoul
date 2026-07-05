#include "face.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "emote_gen_player.h"
#include "panel_st7701.h"

static const char *TAG = "face";

/* Emote canvas is landscape (matches the 640x480 assets); panel is portrait. */
#define CANVAS_W   640
#define CANVAS_H   480
#define EMOTE_PARTITION "emote_gen"

/* Rotate direction: the physical panel is mounted turned 90°. If the face shows
 * up mirrored/upside-down on the bench, flip this. */
#ifndef FACE_ROTATE_CCW
#define FACE_ROTATE_CCW 0
#endif

/* Translucent HUD veil: the overlay panel dims + gray-tints the emote behind it
 * (see-through) instead of a solid block. Tunable: VEIL_KEEP/16 of the original
 * face pixel is kept, the rest mixes in the dark-gray tint. */
#define VEIL_KEEP    9    /* /16 face kept (higher = more see-through) */
#define VEIL_TINT_R  5    /* dark-gray tint, RGB565 channel scale (5/6/5) */
#define VEIL_TINT_G  10
#define VEIL_TINT_B  5

static emote_gen_player_handle_t s_player;
static esp_lcd_panel_handle_t    s_panel;
static uint16_t                 *s_rot;       /* rotated flush scratch (PSRAM) */
static size_t                    s_rot_px;    /* capacity in pixels */
static volatile uint32_t         s_frames;

/* Veil rectangle in canvas coords (x2<=x1 => disabled). Set by the HUD so the
 * face module needn't know HUD geometry. */
static volatile int s_veil_x1, s_veil_y1, s_veil_x2, s_veil_y2;

static inline uint16_t veil_px(uint16_t p)
{
    uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    r = (r * VEIL_KEEP + VEIL_TINT_R * (16 - VEIL_KEEP)) >> 4;
    g = (g * VEIL_KEEP + VEIL_TINT_G * (16 - VEIL_KEEP)) >> 4;
    b = (b * VEIL_KEEP + VEIL_TINT_B * (16 - VEIL_KEEP)) >> 4;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void face_set_veil_rect(int x, int y, int w, int h)
{
    s_veil_x1 = x;
    s_veil_y1 = y;
    s_veil_x2 = x + w;
    s_veil_y2 = y + h;
}

void face_clear_veil(void)
{
    s_veil_x2 = s_veil_x1 = 0;
}

/* switch queue: keep anim_fade/anim_now off the gfx render task */
typedef struct {
    char name[EMOTE_GEN_PLAYER_STR_MAX];
    bool fade;
} face_switch_msg_t;
static QueueHandle_t s_switch_q;

/* --- rotated flush: gfx gives a canvas region; transpose into the panel --- */
static void flush_cb(int x1, int y1, int x2, int y2, const void *data,
                     emote_gen_player_handle_t manager)
{
    (void)manager;
    const uint16_t *src = (const uint16_t *)data;
    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) {
        return;
    }

    size_t need = (size_t)w * h;
    if (need > s_rot_px) {
        uint16_t *grown = heap_caps_realloc(s_rot, need * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!grown) {
            ESP_LOGE(TAG, "rot buf realloc %u px failed", (unsigned)need);
            return;
        }
        s_rot = grown;
        s_rot_px = need;
    }

    /* Does this flush region touch the veil rect? Only then pay the per-pixel
     * blend; the common case (no overlap) keeps the plain fast copy. */
    bool veil = s_veil_x2 > s_veil_x1 &&
                x1 < s_veil_x2 && x2 > s_veil_x1 && y1 < s_veil_y2 && y2 > s_veil_y1;

#if FACE_ROTATE_CCW
    /* px in [y1,y2), py in [PANEL_V_RES-x2, PANEL_V_RES-x1); pw = h */
    for (int cy = 0; cy < h; cy++) {
        for (int cx = 0; cx < w; cx++) {
            uint16_t p = src[cy * w + cx];
            if (veil) {
                int ax = x1 + cx, ay = y1 + cy;
                if (ax >= s_veil_x1 && ax < s_veil_x2 && ay >= s_veil_y1 && ay < s_veil_y2) {
                    p = veil_px(p);
                }
            }
            s_rot[(w - 1 - cx) * h + cy] = p;
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, y1, PANEL_V_RES - x2, y2, PANEL_V_RES - x1, s_rot);
#else
    /* 90° CW: px in [PANEL_H_RES-y2, PANEL_H_RES-y1), py in [x1,x2); pw = h */
    for (int cy = 0; cy < h; cy++) {
        for (int cx = 0; cx < w; cx++) {
            uint16_t p = src[cy * w + cx];
            if (veil) {
                int ax = x1 + cx, ay = y1 + cy;
                if (ax >= s_veil_x1 && ax < s_veil_x2 && ay >= s_veil_y1 && ay < s_veil_y2) {
                    p = veil_px(p);
                }
            }
            s_rot[cx * h + (h - 1 - cy)] = p;
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, PANEL_H_RES - y2, x1, PANEL_H_RES - y1, x2, s_rot);
#endif
}

static void update_cb(gfx_disp_event_t event, const void *obj, emote_gen_player_handle_t manager)
{
    (void)obj;
    (void)manager;
    if (event == GFX_DISP_EVENT_ONE_FRAME_DONE || event == GFX_DISP_EVENT_ALL_FRAME_DONE) {
        s_frames++;
    }
}

static bool dpi_trans_done(esp_lcd_panel_handle_t panel,
                           esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    emote_gen_player_handle_t h = (emote_gen_player_handle_t)user_ctx;
    if (h) {
        emote_gen_player_notify_flush_finished(h);
    }
    return false;
}

static void switch_worker(void *arg)
{
    (void)arg;
    face_switch_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_switch_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        esp_err_t err = msg.fade
                            ? emote_gen_player_anim_fade_name(s_player, msg.name, true)
                            : emote_gen_player_anim_now_name(s_player, msg.name, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "switch to '%s' (%s) failed: %s", msg.name,
                     msg.fade ? "fade" : "now", esp_err_to_name(err));
        }
    }
}

esp_err_t face_init(void)
{
    ESP_RETURN_ON_ERROR(st7701_init(), TAG, "panel");
    s_panel = st7701_get_panel();
    ESP_RETURN_ON_FALSE(s_panel, ESP_FAIL, TAG, "no panel");

    /* start with a modest rotated buffer; flush_cb grows it if a bigger region
     * ever arrives (full-frame emote flush = 640*480). */
    s_rot_px = CANVAS_W * 32;
    s_rot = heap_caps_malloc(s_rot_px * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_rot, ESP_ERR_NO_MEM, TAG, "rot buf");

    emote_gen_player_config_t cfg = {
        .flags = {
            .swap = false,
            .double_buffer = true,
            .buff_dma = true,
            .buff_spiram = true,   /* draw buffers in PSRAM — internal RAM is scarce
                                    * with gfx+wifi+esp-dl in one app */
        },
        .gfx_emote = {
            .h_res = CANVAS_W,
            .v_res = CANVAS_H,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = CANVAS_W * 16,
        },
        .task = {
            .task_priority = 7,
            .task_stack = 6 * 1024,
            .task_affinity = 1,   /* keep gfx off the control core */
            .task_stack_in_ext = false,
        },
        .flush_cb = flush_cb,
        .update_cb = update_cb,
    };

    s_player = emote_gen_player_init(&cfg);
    ESP_RETURN_ON_FALSE(s_player, ESP_FAIL, TAG, "player init");

    ESP_RETURN_ON_ERROR(st7701_register_color_trans_done_callback(dpi_trans_done, s_player),
                        TAG, "dpi cb");

    emote_gen_player_data_t assets = {
        .type = EMOTE_GEN_PLAYER_SOURCE_PARTITION,
        .source = { .partition_label = EMOTE_PARTITION },
        .flags = { .mmap_enable = 1 },
    };
    ESP_RETURN_ON_ERROR(emote_gen_player_mount_assets(s_player, &assets), TAG, "mount emote_gen");
    ESP_LOGI(TAG, "emote pack mounted: %u clip(s)",
             (unsigned)emote_gen_player_get_index_count(s_player));

    s_switch_q = xQueueCreate(8, sizeof(face_switch_msg_t));
    ESP_RETURN_ON_FALSE(s_switch_q, ESP_ERR_NO_MEM, TAG, "switch q");
    xTaskCreatePinnedToCore(switch_worker, "face_sw", 4096, NULL, 6, NULL, 0);

    face_set_emotion("waiting", FACE_NOW);
    return ESP_OK;
}

esp_err_t face_set_emotion(const char *name, face_switch_mode_t mode)
{
    if (!s_switch_q || !name || !name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    face_switch_msg_t msg = { .fade = (mode == FACE_FADE) };
    strlcpy(msg.name, name, sizeof(msg.name));
    return xQueueSend(s_switch_q, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t face_set_tip(const char *utf8)
{
    return s_player ? emote_gen_player_set_tip_text(s_player, utf8) : ESP_ERR_INVALID_STATE;
}

gfx_disp_t *face_gfx_disp(void)
{
    return s_player ? emote_gen_player_get_disp(s_player) : NULL;
}

gfx_handle_t face_gfx_handle(void)
{
    return s_player ? emote_gen_player_get_gfx_handle(s_player) : NULL;
}

uint32_t face_frame_count(void)
{
    return s_frames;
}

bool face_ready(void)
{
    return s_player != NULL;
}

int face_emote_clip_count(void)
{
    return s_player ? (int)emote_gen_player_get_index_count(s_player) : -1;
}
