#include "ui_core.h"

#include "audio_core.h"
#include "diag_core.h"
#include "ui_lvgl_port.h"

#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui_core";

typedef struct {
    const char *name;
    lv_obj_t *value;
} status_row_t;

static ui_page_t s_page = UI_PAGE_HOME;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_touch_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static status_row_t s_rows[11] = {0};

static void set_row(size_t index, const char *name, hw_status_t status)
{
    if (index >= sizeof(s_rows) / sizeof(s_rows[0]) || !s_rows[index].value) {
        return;
    }

    s_rows[index].name = name;
    lv_label_set_text_fmt(s_rows[index].value, "%s", diag_core_hw_status_name(status));

    lv_color_t color = lv_color_hex(0x6B7280);
    if (status == HW_STATUS_OK) {
        color = lv_palette_main(LV_PALETTE_GREEN);
    } else if (status == HW_STATUS_ERROR) {
        color = lv_palette_main(LV_PALETTE_RED);
    } else if (status == HW_STATUS_DISABLED) {
        color = lv_color_hex(0x6B7280);
    } else if (status == HW_STATUS_ABSENT) {
        color = lv_palette_main(LV_PALETTE_ORANGE);
    }
    lv_obj_set_style_text_color(s_rows[index].value, color, 0);
}

static void refresh_status(void)
{
    const bsp_board_status_t *status = diag_core_get_board_status();
    set_row(0, "Display", status->display);
    set_row(1, "Touch", status->touch);
    set_row(2, "Audio", status->audio);
    set_row(3, "SD", status->storage);
    set_row(4, "Camera", status->camera);
    set_row(5, "Wi-Fi", status->wifi);
    set_row(6, "BH1750", status->bh1750);
    set_row(7, "VL6180X-L", status->vl6180x_l);
    set_row(8, "VL6180X-C", status->vl6180x_c);
    set_row(9, "VL6180X-R", status->vl6180x_r);
    set_row(10, "Motion", status->motion);

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev && s_touch_label) {
        lv_point_t point = {0};
        lv_indev_get_point(indev, &point);
        lv_indev_state_t state = lv_indev_get_state(indev);
        lv_label_set_text_fmt(s_touch_label, "Touch: %s  X:%d Y:%d",
                              state == LV_INDEV_STATE_PRESSED ? "PRESSED" : "released",
                              (int)point.x, (int)point.y);
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status();
}

static void tone_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    esp_err_t err = audio_core_play_test_tone(880, 200);
    ESP_LOGI(TAG, "audio test tone: %s", esp_err_to_name(err));
    refresh_status();
}

static lv_obj_t *create_status_row(lv_obj_t *parent, int y, const char *name, size_t index)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 28, y);

    lv_obj_t *value = lv_label_create(parent);
    lv_label_set_text(value, "ABSENT");
    lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -28, y);
    s_rows[index].name = name;
    s_rows[index].value = value;
    return value;
}

static esp_err_t create_diag_home(void)
{
    s_screen = lv_obj_create(NULL);
    if (!s_screen) {
        return ESP_FAIL;
    }
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xF3F4F6), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "NanoSoul HW Baseline");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111827), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_size(panel, 432, 418);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);

    const char *names[] = {
        "Display", "Touch", "Audio", "SD", "Camera", "Wi-Fi",
        "BH1750", "VL6180X-L", "VL6180X-C", "VL6180X-R", "Motion",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        create_status_row(panel, 22 + (int)i * 34, names[i], i);
    }

    s_touch_label = lv_label_create(s_screen);
    lv_label_set_text(s_touch_label, "Touch: released  X:0 Y:0");
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0x374151), 0);
    lv_obj_align(s_touch_label, LV_ALIGN_TOP_MID, 0, 492);

    lv_obj_t *tone_btn = lv_button_create(s_screen);
    lv_obj_set_size(tone_btn, 160, 48);
    lv_obj_align(tone_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_radius(tone_btn, 8, 0);
    lv_obj_add_event_cb(tone_btn, tone_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tone_label = lv_label_create(tone_btn);
    lv_label_set_text(tone_label, "Audio Tone");
    lv_obj_center(tone_label);

    lv_screen_load(s_screen);
    refresh_status();
    return ESP_OK;
}

esp_err_t ui_core_init(void)
{
    esp_err_t err = ui_lvgl_port_init();
    if (err != ESP_OK) {
        return err;
    }
    err = create_diag_home();
    if (err != ESP_OK) {
        return err;
    }
    err = ui_lvgl_port_start();
    if (err != ESP_OK) {
        return err;
    }
    if (!s_refresh_timer) {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, 500, NULL);
    }
    s_page = UI_PAGE_HOME;
    ESP_LOGI(TAG, "diag UI ready");
    return ESP_OK;
}

esp_err_t ui_core_show_page(ui_page_t page)
{
    s_page = page;
    return ESP_OK;
}

ui_page_t ui_core_get_page(void)
{
    return s_page;
}
