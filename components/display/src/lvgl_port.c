// LVGL ↔ ESP-LCD 胶水层 + display.h 公共入口。
// 取自 NanoSoul-Alpha ui_lvgl_port.c；触摸走 FT6x36（board I²C0）接 LVGL indev。
#include "display.h"
#include "st7701.h"
#include "touch.h"

#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";

#define LVGL_DRAW_BUF_LINES     64
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_STACK_SIZE    (6 * 1024)
#define LVGL_TASK_PRIORITY      4
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  10

static _lock_t s_lvgl_api_lock;
static lv_display_t *s_display = NULL;
static lv_indev_t *s_indev = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static TaskHandle_t s_lvgl_task = NULL;
static void *s_buf1 = NULL;
static void *s_buf2 = NULL;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static IRAM_ATTR bool notify_flush_ready(esp_lcd_panel_handle_t panel,
                                         esp_lcd_dpi_panel_event_data_t *edata,
                                         void *user_ctx)
{
    (void)panel;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Starting LVGL task");
    while (1) {
        _lock_acquire(&s_lvgl_api_lock);
        uint32_t delay_ms = lv_timer_handler();
        _lock_release(&s_lvgl_api_lock);

        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * delay_ms);
    }
}

static esp_err_t lvgl_port_init(void)
{
    if (s_display) {
        return ESP_OK;
    }

    lv_init();

    s_display = lv_display_create(DISP_H_RES, DISP_V_RES);
    ESP_RETURN_ON_FALSE(s_display, ESP_FAIL, TAG, "lv display");

    size_t draw_buffer_sz = DISP_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    s_buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    s_buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_buf1 && s_buf2, ESP_ERR_NO_MEM, TAG, "lvgl buffers");

    lv_display_set_user_data(s_display, st7701_get_panel());
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_buf1, s_buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flush_cb);

    ESP_RETURN_ON_ERROR(st7701_register_color_trans_done_callback(notify_flush_ready, s_display),
                        TAG, "flush ready cb");

    const esp_timer_create_args_t tick_timer_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000),
                        TAG, "tick start");

    ESP_LOGI(TAG, "LVGL port ready");
    return ESP_OK;
}

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(st7701_init(), TAG, "st7701");
    ESP_RETURN_ON_ERROR(lvgl_port_init(), TAG, "lvgl port");

    if (!s_lvgl_task) {
        BaseType_t ok = xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL,
                                    LVGL_TASK_PRIORITY, &s_lvgl_task);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "lvgl task");
    }
    return ESP_OK;
}

void display_lock(void)
{
    _lock_acquire(&s_lvgl_api_lock);
}

void display_unlock(void)
{
    _lock_release(&s_lvgl_api_lock);
}

lv_display_t *display_get_lv(void)
{
    return s_display;
}

// —— 触摸 ——
// FT6x36 返回裸坐标；面板竖装 480×640，LVGL 直画面板（flush 不旋转）。
// 变换取自 debugui 已验证的 raw_to_canvas（canvas 640×480: cx=639-raw.y, cy=raw.x）
// 复合「canvas 90°CW → 480×640 面板」，化简为直接的 raw→竖屏映射：
//   lvgl_x = 479 - raw.x ,  lvgl_y = 639 - raw.y
// 两轴镜像/轴向是面板触摸装配方向决定的——上板若点偏/点反，只调这两行的
// 取反与 x/y 互换（参照 debugui 的口径）。
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_point_t pt;
    bool pressed = false;
    if (touch_read_raw(&pt, &pressed) != ESP_OK || !pressed) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int32_t x = (int32_t)(DISP_H_RES - 1) - (int32_t)pt.x;   // 479 - raw.x
    int32_t y = (int32_t)(DISP_V_RES - 1) - (int32_t)pt.y;   // 639 - raw.y
    if (x < 0) x = 0; else if (x > DISP_H_RES - 1) x = DISP_H_RES - 1;
    if (y < 0) y = 0; else if (y > DISP_V_RES - 1) y = DISP_V_RES - 1;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t display_touch_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = touch_init(bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch absent (%s) — 屏可显示但无输入", esp_err_to_name(err));
        return err;   // non-fatal to the caller; display still works
    }
    _lock_acquire(&s_lvgl_api_lock);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(s_indev, s_display);
    lv_indev_set_read_cb(s_indev, touch_read_cb);
    _lock_release(&s_lvgl_api_lock);
    ESP_LOGI(TAG, "touch indev ready");
    return ESP_OK;
}
