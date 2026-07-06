// NanoSoul 整机固件入口。
// Boot mode: an IO48-to-GND strap at boot forces TEST mode; otherwise the
// Kconfig NANOSOUL_MODE choice decides:
//   FACE     — emote face + debug HUD (wired from Phase A onward).
//   SELFTEST — auto-loop diagnostics; JSON matrix to serial.
//   TEST     — full runtime + test services (override/motor_test/serial link).
//   TESTPANEL— legacy LVGL three-motor bring-up panel.
// ESP-IDF v5.5.2 / ESP32-P4. flash/monitor 由持板者本地手动跑（板外纪律）。
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_face.h"
#include "app_selftests.h"
#include "app_testmode.h"
#include "esp_netif.h"
#include "ns_config.h"
#include "sd_storage.h"
#include "selftest.h"
#include "simsense.h"
#include "telemetry.h"

#if CONFIG_NANOSOUL_MODE_TESTPANEL
#include "display.h"
#include "test_app.h"
#endif

// Boot strap: short IO48 to GND to force TEST mode (internal pull-up; conflict
// fallback = IO33 — change only this one line). Read once at boot, then release.
#define TEST_STRAP_GPIO GPIO_NUM_48

static const char *TAG = "nanosoul";

static bool strap_test_mode(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TEST_STRAP_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(10));
    int low = 0;
    for (int i = 0; i < 5; i++) {
        low += (gpio_get_level(TEST_STRAP_GPIO) == 0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    gpio_reset_pin(TEST_STRAP_GPIO);
    return low >= 4;   /* strapped to GND */
}

static const char *kconfig_mode_name(void)
{
#if CONFIG_NANOSOUL_MODE_FACE
    return "FACE";
#elif CONFIG_NANOSOUL_MODE_SELFTEST
    return "SELFTEST";
#elif CONFIG_NANOSOUL_MODE_TEST
    return "TEST";
#else
    return "TESTPANEL";
#endif
}

// Bring up the always-on core: NVS, telemetry hub, selftest registry, SD card
// + config. Runs in every mode.
static void init_common(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(telemetry_init());
    ESP_ERROR_CHECK(selftest_init());
    ESP_ERROR_CHECK(simsense_init());   /* 传感覆盖层：所有模式常驻，非测试时整表空 */

    /* Bring up the TCP/IP stack unconditionally so the companion WS server can
     * start even when no Wi-Fi is configured (netlink skips it when offline). */
    ESP_ERROR_CHECK(esp_netif_init());

    if (sd_storage_mount(SD_MOUNT_POINT) != ESP_OK) {
        ESP_LOGW(TAG, "SD not mounted; continuing on default config");
    }
    ns_config_init(NS_CONFIG_PATH); // loads defaults even without a card

    app_selftests_register();
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    bool strap = strap_test_mode();
#if CONFIG_NANOSOUL_MODE_TEST
    bool test_mode = true;             // Kconfig forces TEST (board-off, no jumper)
#else
    bool test_mode = strap;            // strap overrides the Kconfig default
#endif

    ESP_LOGI(TAG, "NanoSoul boot — kconfig=%s strap=%d -> %s, ESP-IDF %s, target %s, %d core(s)",
             kconfig_mode_name(), strap, test_mode ? "TEST" : kconfig_mode_name(),
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.cores);

    init_common();

    if (test_mode) {
        app_test_run();                // full runtime + test services (docs/13)
        return;
    }

#if CONFIG_NANOSOUL_MODE_TESTPANEL
    ESP_ERROR_CHECK(display_init());   // ST7701 + LVGL
    ESP_ERROR_CHECK(test_app_start()); // 电机/编码器/电流 + 自动循环 + 面板
#elif CONFIG_NANOSOUL_MODE_SELFTEST
    app_face_run(true);   // full runtime + auto-loop diagnostics
#else /* FACE */
    app_face_run(false);  // emote face + debug HUD (product/demo)
#endif
}
