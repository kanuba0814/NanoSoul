#include "app_testmode.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_face.h"
#include "drv_motor.h"
#include "hud.h"
#include "motion.h"
#include "netlink.h"
#include "ns_config.h"
#include "soul.h"
#include "telemetry.h"
#include "testlink.h"

static const char *TAG = "testmode";

/* ---------------- motor_test one-shot burst ---------------- */
/* Drives a single wheel open-loop for a bounded time. The motion apply bridge is
 * detached for the duration so soul/teleop can't fight the manual drive, and
 * re-attached (if motion owns the wheels) when the burst ends. */

static volatile bool s_burst_active;
static volatile int  s_burst_motor;
static esp_timer_handle_t s_burst_timer;

static void burst_end_cb(void *arg)
{
    (void)arg;
    motor_set(s_burst_motor, MOTOR_COAST, 0);
    if (motion_enabled()) {
        motion_set_apply(app_face_motor_apply);   /* hand the wheels back to motion */
    }
    s_burst_active = false;
}

static bool test_motor_run(int m, int duty, int ms)
{
    if (s_burst_active) {
        return false;   /* one at a time */
    }
    s_burst_active = true;
    s_burst_motor = m;
    motion_set_apply(NULL);          /* take the wheels away from motion */
    motors_enable(true);
    motor_dir_t dir = duty > 0 ? MOTOR_FORWARD : (duty < 0 ? MOTOR_REVERSE : MOTOR_COAST);
    motor_set(m, dir, abs(duty));

    if (!s_burst_timer) {
        esp_timer_create_args_t a = { .callback = burst_end_cb, .name = "motor_burst" };
        if (esp_timer_create(&a, &s_burst_timer) != ESP_OK) {
            burst_end_cb(NULL);   /* no timer: stop now, treat as a very short pulse */
            return true;
        }
    }
    esp_timer_start_once(s_burst_timer, (uint64_t)ms * 1000);
    return true;
}

static void test_motor_stop(void)
{
    if (s_burst_timer) {
        esp_timer_stop(s_burst_timer);
    }
    motor_stop_all();
    motors_enable(false);            /* STBY low = hard cut */
    if (motion_enabled()) {
        motion_set_apply(app_face_motor_apply);
    }
    s_burst_active = false;
}

/* ---------------- long-press E-stop ---------------- */
/* A long press anywhere on the screen is the physical panic button in test mode:
 * cut the motors and drop soul into FAULT. */
static void on_touch_estop(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if ((ns_event_id_t)id == NS_EVT_TOUCH) {
        ns_evt_touch_t *e = data;
        if (e && e->long_press) {
            test_motor_stop();
            soul_notify_fault("estop (screen long-press)");
            ESP_LOGW(TAG, "E-STOP: screen long-press");
        }
    }
}

/* ---------------- connection-info banner ---------------- */
static void banner_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    bool logged_up = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        char ip[16] = {0};
        netlink_get_ip(ip, sizeof(ip));
        bool up = netlink_is_up();
        if (up && !logged_up) {
            logged_up = true;
            ESP_LOGW(TAG, "==== TEST MODE ====");
            ESP_LOGW(TAG, " WS   : ws://%s/ws%s", ip,
                     cfg->companion.token[0] ? "?token=<see config>" : "");
            ESP_LOGW(TAG, " mDNS : ws://nanosoul.local/ws");
            ESP_LOGW(TAG, " serial: USB-Serial-JTAG NDJSON %s",
                     testlink_serial_running() ? "up" : "down");
        } else if (!up) {
            logged_up = false;
        }
    }
}

/* ---------------- entry ---------------- */
/* One-shot boot diagnostic: list /sdcard (two levels) so config-path mistakes
 * are visible on the console when the SD is inside the board. */
static void dump_sdcard(void)
{
    ESP_LOGW(TAG, "==== SD /sdcard listing ====");
    DIR *d = opendir("/sdcard");
    if (!d) {
        ESP_LOGW(TAG, "  opendir(/sdcard) failed — card not mounted");
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        bool dir = (e->d_type == DT_DIR);
        ESP_LOGW(TAG, "  /sdcard/%s%s", e->d_name, dir ? "/" : "");
        if (dir) {
            char sub[300];   /* "/sdcard/" + up to 255-byte name + NUL */
            snprintf(sub, sizeof(sub), "/sdcard/%s", e->d_name);
            DIR *d2 = opendir(sub);
            if (d2) {
                struct dirent *e2;
                while ((e2 = readdir(d2)) != NULL) {
                    ESP_LOGW(TAG, "      %s/%s", sub, e2->d_name);
                }
                closedir(d2);
            }
        }
    }
    closedir(d);
    ESP_LOGW(TAG, "==== end SD listing ====");
}

void app_test_run(void)
{
    ESP_LOGW(TAG, "entering TEST mode (docs/13)");
    dump_sdcard();
    ns_proto_set_test_mode(true);

    /* Full FACE runtime: soul runs, companion WS up, all sensors live. Injected
     * overrides therefore drive the real decision path. */
    app_face_run(false);

    /* Ensure the H-bridges are usable for motor_test even if the SD config left
     * motion disabled (compute-only). If motion IS enabled, app_face_run already
     * init'd + enabled them. */
    if (!motion_enabled()) {
        if (motors_init() == ESP_OK) {
            motors_enable(true);
        } else {
            ESP_LOGW(TAG, "motors_init failed; motor_test unavailable");
        }
    }
    ns_motor_hooks_t hooks = { .test_run = test_motor_run, .stop_all = test_motor_stop };
    ns_proto_set_motor_hooks(&hooks);

    /* Wi-Fi-independent transport. */
    testlink_serial_start();

    /* Force the connection-info HUD on (shows IP + full sensor/decision readout),
     * so whoever powers a strapped board knows where to connect. */
    if (!hud_ready()) {
        if (hud_init() == ESP_OK) {
            hud_start();
        }
    } else {
        hud_set_enabled(true);
    }

    /* Screen long-press = E-stop. */
    esp_event_handler_instance_register(NANOSOUL_EVENT, NS_EVT_TOUCH,
                                        on_touch_estop, NULL, NULL);

    xTaskCreatePinnedToCore(banner_task, "tl_banner", 3072, NULL, 2, NULL, 0);
    ESP_LOGW(TAG, "TEST mode up: motor_test + serial link + HUD + long-press estop");
}
