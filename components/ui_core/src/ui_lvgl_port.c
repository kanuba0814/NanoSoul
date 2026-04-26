#include "ui_lvgl_port.h"
#include "bsp_board_pins.h"
#include "bsp_display_st7701.h"
#include "bsp_touch_ft6x36.h"

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
static lv_indev_t *s_touch_indev = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static TaskHandle_t s_lvgl_task = NULL;
static void *s_buf1 = NULL;
static void *s_buf2 = NULL;

static void ui_lvgl_port_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static IRAM_ATTR bool ui_lvgl_port_notify_flush_ready(esp_lcd_panel_handle_t panel,
                                                   esp_lcd_dpi_panel_event_data_t *edata,
                                                   void *user_ctx)
{
    (void)panel;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void ui_lvgl_port_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    bsp_touch_point_t point = {0};
    bool pressed = false;

    if (bsp_touch_ft6x36_read_point(&point, &pressed) == ESP_OK && pressed) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_lvgl_port_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void ui_lvgl_port_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Starting LVGL task");

    while (1) {
        uint32_t delay_ms = 0;
        _lock_acquire(&s_lvgl_api_lock);
        delay_ms = lv_timer_handler();
        _lock_release(&s_lvgl_api_lock);

        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * delay_ms);
    }
}

esp_err_t ui_lvgl_port_init(void)
{
    if (s_display) {
        return ESP_OK;
    }

    lv_init();

    s_display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    ESP_RETURN_ON_FALSE(s_display, ESP_FAIL, TAG, "lv display");

    size_t draw_buffer_sz = BSP_LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    s_buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    s_buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_buf1 && s_buf2, ESP_ERR_NO_MEM, TAG, "lvgl buffers");

    lv_display_set_user_data(s_display, bsp_display_st7701_get_panel());
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_buf1, s_buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, ui_lvgl_port_flush_cb);

    ESP_RETURN_ON_ERROR(bsp_display_st7701_register_color_trans_done_callback(
                            ui_lvgl_port_notify_flush_ready, s_display),
                        TAG, "flush ready cb");

    s_touch_indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(s_touch_indev, ESP_FAIL, TAG, "touch indev");
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(s_touch_indev, s_display);
    lv_indev_set_read_cb(s_touch_indev, ui_lvgl_port_touch_read_cb);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = ui_lvgl_port_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000),
                        TAG, "tick start");

    ESP_LOGI(TAG, "LVGL port ready");
    return ESP_OK;
}

esp_err_t ui_lvgl_port_start(void)
{
    if (s_lvgl_task) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(ui_lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL,
                                LVGL_TASK_PRIORITY, &s_lvgl_task);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

lv_display_t *ui_lvgl_port_get_display(void)
{
    return s_display;
}

void ui_lvgl_port_lock(void)
{
    _lock_acquire(&s_lvgl_api_lock);
}

void ui_lvgl_port_unlock(void)
{
    _lock_release(&s_lvgl_api_lock);
}
