// NanoSoul 整机固件入口。
// Boot mode (Kconfig NANOSOUL_MODE):
//   FACE     — emote face + debug HUD (wired from Phase A onward).
//   SELFTEST — auto-loop diagnostics; JSON matrix to serial.
//   TESTPANEL— legacy LVGL three-motor bring-up panel.
// ESP-IDF v5.5.2 / ESP32-P4. flash/monitor 由持板者本地手动跑（板外纪律）。
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_selftests.h"
#include "esp_netif.h"
#include "ns_config.h"
#include "sd_storage.h"
#include "selftest.h"
#include "simsense.h"
#include "telemetry.h"

#if CONFIG_NANOSOUL_MODE_TESTPANEL
#include "display.h"
#include "test_app.h"
#else
#include "app_face.h"
#endif

static const char *TAG = "nanosoul";

static const char *boot_mode_name(void)
{
#if CONFIG_NANOSOUL_MODE_FACE
    return "FACE";
#elif CONFIG_NANOSOUL_MODE_SELFTEST
    return "SELFTEST";
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
    ESP_LOGI(TAG, "NanoSoul boot — mode=%s, ESP-IDF %s, target %s, %d core(s)",
             boot_mode_name(), esp_get_idf_version(), CONFIG_IDF_TARGET, chip.cores);

    init_common();

#if CONFIG_NANOSOUL_MODE_TESTPANEL
    ESP_ERROR_CHECK(display_init());   // ST7701 + LVGL
    ESP_ERROR_CHECK(test_app_start()); // 电机/编码器/电流 + 自动循环 + 面板
#elif CONFIG_NANOSOUL_MODE_SELFTEST
    app_face_run(true);   // full runtime + auto-loop diagnostics
#else /* FACE */
    app_face_run(false);  // emote face + debug HUD (product/demo)
#endif
}
