#include "touch_router.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "telemetry.h"

static const char *TAG = "touch_router";

#define POLL_MS        50    /* 20 Hz */
#define LONG_PRESS_MS 800

static touch_point_t s_last;
static bool          s_pressed;
static bool          s_ready;

static void router_task(void *arg)
{
    (void)arg;
    bool    was = false;
    int64_t down_since = 0;
    bool    long_fired = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        touch_point_t pt;
        bool          pressed = false;
        if (touch_read_raw(&pt, &pressed) != ESP_OK) {
            pressed = false;
        }
        if (pressed) {
            s_last = pt;
        }
        s_pressed = pressed;

        int64_t now = esp_timer_get_time() / 1000;
        if (pressed && !was) {
            down_since = now;
            long_fired = false;
            ns_evt_touch_t e = { .long_press = false };
            telemetry_post(NS_EVT_TOUCH, &e, sizeof(e));
        } else if (pressed && !long_fired && (now - down_since) >= LONG_PRESS_MS) {
            long_fired = true;
            ns_evt_touch_t e = { .long_press = true };
            telemetry_post(NS_EVT_TOUCH, &e, sizeof(e));
        }
        was = pressed;
    }
}

esp_err_t touch_router_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = touch_init(bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch panel init failed (%s) — touch disabled", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    xTaskCreatePinnedToCore(router_task, "touch", 2560, NULL, 3, NULL, 0);
    return ESP_OK;
}

bool touch_router_ready(void) { return s_ready; }

void touch_router_last(touch_point_t *pt, bool *pressed)
{
    if (pt) {
        *pt = s_last;
    }
    if (pressed) {
        *pressed = s_pressed;
    }
}
