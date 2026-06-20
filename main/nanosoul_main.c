// NanoSoul 固件入口（骨架）。本版只做最小启动日志，不触硬件 → 板外可编译。
// 子系统（运动/视觉/感知决策状态机/表情/Agent）后续按 docs/04 五阶段路线接管上来。
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nanosoul";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "NanoSoul boot");
    ESP_LOGI(TAG, "ESP-IDF %s, target %s, %d core(s)",
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.cores);

    // TODO（按 docs/04 路线）：BSP 初始化 → 屏/相机/联网 → 本地感知决策状态机
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive");
    }
}
