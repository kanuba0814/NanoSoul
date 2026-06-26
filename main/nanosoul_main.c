// NanoSoul 单电机桥(M2)+编码器 测试固件入口（test 分支）。
// 上电：点屏 → 起 LVGL → 自动循环驱动 M2，状态实时显示在屏上。
// 一律 ESP-IDF v5.5.2 / ESP32-P4。flash/monitor 由持板者本地 /dev/ttyACM0 手动跑。
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "test_app.h"

static const char *TAG = "nanosoul";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "NanoSoul M2 TEST boot");
    ESP_LOGI(TAG, "ESP-IDF %s, target %s, %d core(s)",
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.cores);

    ESP_ERROR_CHECK(display_init());     // ST7701 + LVGL
    ESP_ERROR_CHECK(test_app_start());   // 电机/编码器/电流 + 自动循环 + 面板
}
