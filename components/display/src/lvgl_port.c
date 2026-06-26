// LVGL ↔ ESP-LCD 胶水层 + display.h 公共入口。
// 取自 NanoSoul-Alpha ui_lvgl_port.c，去掉 FT6x36 触摸（本测试无输入）。
#include "display.h"
#include "st7701.h"

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
