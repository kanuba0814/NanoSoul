#include "app_face.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "face.h"
#include "hud.h"
#include "ns_config.h"
#include "selftest.h"

static const char *TAG = "app_face";

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

    /* Later phases extend the bring-up here: camera, vision, soul, netlink,
     * voice — all before the selftest loop so its checks probe a live system. */

    if (run_selftest_loop) {
        selftest_run_loop(5000); // never returns
    }
    // FACE product mode: runtime tasks (gfx render, hud) carry on; app_main returns.
    ESP_LOGI(TAG, "face runtime up (soul state machine lands in Phase C)");
}
