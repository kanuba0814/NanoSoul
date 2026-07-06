#include "testlink.h"

#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*
 * serial_link — USB-Serial-JTAG NDJSON transport (docs/13, T4).
 *
 * A Wi-Fi-independent test channel: one JSON object per line in, one per line
 * out. Shares the exact same command dispatch (ns_proto_handle) and frame sinks
 * (state/event/sense) as the companion WS transport. Requires the secondary
 * console to be NONE (sdkconfig) so the USJ peripheral is free; logs stay on
 * UART0. The host tells protocol from stray log lines by the leading '{'.
 */

static const char *TAG = "tl_serial";

#define SERIAL_LINE_MAX 1024

static SemaphoreHandle_t s_write_lock;
static bool              s_running;

static void serial_write_line(const char *json)
{
    if (!json || !s_write_lock) {
        return;
    }
    size_t len = strlen(json);
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    usb_serial_jtag_write_bytes(json, len, pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_write_lock);
}

/* reply callback for command acks (same write path). */
static void serial_reply(void *ctx, const char *json)
{
    (void)ctx;
    serial_write_line(json);
}

static void serial_rx_task(void *arg)
{
    (void)arg;
    static char line[SERIAL_LINE_MAX];
    size_t len = 0;
    uint8_t ch;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            if (len > 0) {
                line[len] = 0;
                ns_proto_handle(line, len, serial_reply, NULL);
                len = 0;
            }
            continue;
        }
        if (len < SERIAL_LINE_MAX - 1) {
            line[len++] = (char)ch;
        } else {
            len = 0;   /* overrun: drop the runaway line */
        }
    }
}

esp_err_t testlink_serial_start(void)
{
    if (s_running) {
        return ESP_OK;
    }
    s_write_lock = xSemaphoreCreateMutex();
    if (!s_write_lock) {
        return ESP_ERR_NO_MEM;
    }
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USJ driver install failed: %s", esp_err_to_name(err));
        return err;
    }
    testlink_core_start();               /* state push + event forwarding */
    ns_link_add_sink(serial_write_line); /* state/event/sense also to serial */
    if (xTaskCreatePinnedToCore(serial_rx_task, "tl_rx", 4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }
    s_running = true;
    ESP_LOGI(TAG, "serial NDJSON link up (USB-Serial-JTAG)");
    return ESP_OK;
}

bool testlink_serial_running(void)
{
    return s_running;
}
