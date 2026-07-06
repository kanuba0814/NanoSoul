#include "app_face.h"

#include <stdlib.h>

#include "app_sense.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c0.h"
#include "board_i2c1.h"
#include "camera.h"
#include "companion.h"
#include "debugui.h"
#include "dialog.h"
#include "drv_motor.h"
#include "face.h"
#include "hud.h"
#include "imu.h"
#include "light.h"
#include "motion.h"
#include "netlink.h"
#include "ns_config.h"
#include "selftest.h"
#include "soul.h"
#include "touch_router.h"
#include "vision.h"
#include "voice.h"

static const char *TAG = "app_face";

// Bridge motion's signed per-wheel duty to the TB6612 driver. Only ever called
// when motion is enabled (wheels wired); motion stays free of drv_motor itself.
static void motor_apply(const int16_t duty[3])
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        int16_t d = duty[i];
        motor_dir_t dir = d > 0 ? MOTOR_FORWARD : (d < 0 ? MOTOR_REVERSE : MOTOR_COAST);
        motor_set(i, dir, abs(d));
    }
}

void app_face_run(bool run_selftest_loop)
{
    const ns_config_t *cfg = ns_config_get();

    esp_err_t err = face_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "face_init failed: %s", esp_err_to_name(err));
        // In product mode the face IS the product — fail loud. In selftest mode
        // keep going so the rest of the checks still report.
        if (!run_selftest_loop) {
            ESP_ERROR_CHECK(err);
        }
    }

    if (err == ESP_OK && cfg->debug.overlay) {
        if (hud_init() == ESP_OK) {
            hud_start();
        }
    }
    ESP_LOGI(TAG, "internal heap free after face: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    /* Vision: board I2C0 -> OV5647 camera -> local face detection. Camera is a
     * ready subsystem; a failure logs and continues (the face still runs). */
    if (board_i2c0_init() == ESP_OK) {
        if (camera_init(board_i2c0_bus()) == ESP_OK) {
            camera_start();
            if (vision_init() == ESP_OK) {
                vision_start();
            }
        } else {
            ESP_LOGW(TAG, "camera init failed; vision disabled");
        }
    }

    /* Off-board sensor bus (I2C1, dupont): ambient light now; IMU + motor
     * current land in later milestones. Every sensor degrades gracefully if
     * unplugged (probe misses -> absent, no fault). */
    board_i2c1_init();
    light_init(board_i2c1_bus());
    imu_init(board_i2c1_bus());
    app_sense_start();   /* encoder + current telemetry @10 Hz + WHEEL_MOVED */

    /* Motion + decision. Motors are NOT wired in this build: motion.enabled is
     * false by default, so motion only computes+publishes duty (visible on the
     * HUD) and never touches the motor GPIOs. Only when the config enables it —
     * after the wheels are wired and M1's pins moved off USB — do we init the
     * H-bridge and register the apply bridge. */
    motion_init(cfg->motion.max_duty_pct, cfg->motion.enabled);
    if (cfg->motion.enabled) {
        if (motors_init() == ESP_OK) {
            motors_enable(true);
            motion_set_apply(motor_apply);
        } else {
            ESP_LOGW(TAG, "motors_init failed; staying compute-only");
        }
    }
    if (soul_init() == ESP_OK) {
        soul_start();
    }

    /* Networking (C6 Wi-Fi) + cloud chat pipeline. Offline is fine — the local
     * perception/decision loop above does not depend on any of it. */
    netlink_start();
    dialog_init();

    /* Voice pipeline (shares board I2C0 with the camera SCCB / codec). The mic
     * record path is unverified on this board; a failure just disables voice. */
    if (board_i2c0_bus() && voice_init(board_i2c0_bus()) == ESP_OK) {
        voice_start();
    } else {
        ESP_LOGW(TAG, "voice init skipped/failed");
    }

    /* Companion WebSocket interface for the desktop app (telemetry + control). */
    companion_start();

    /* Touch (FT6x36 on board I2C0): touch_router is the single owner/reader; it
     * feeds both the interaction events (NS_EVT_TOUCH) and the hidden debug UI
     * (10 rapid taps opens the menu). Shares the bus with the camera SCCB/codec. */
    if (board_i2c0_bus() && touch_router_init(board_i2c0_bus()) == ESP_OK) {
        if (debugui_init(board_i2c0_bus()) == ESP_OK) {
            debugui_start();
        }
    }

    if (run_selftest_loop) {
        selftest_run_loop(5000); // never returns
    }
    // FACE product mode: runtime tasks (gfx render, hud) carry on; app_main returns.
    ESP_LOGI(TAG, "face runtime up (soul state machine lands in Phase C)");
}
