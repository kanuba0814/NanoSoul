// NanoSoul test 分支 — 最简屏控硬件自检入口（全本地，无上位机 / 无模式选择）。
// 点屏 + 触摸 + I²C1 传感/电流 → 触摸驱动的三电机自检面板（见 test_app.c）。
// ESP-IDF v5.5.2 / ESP32-P4. flash/monitor 由持板者本地手动跑（板外纪律）。
#include "esp_log.h"

#include "board_i2c0.h"
#include "board_i2c1.h"
#include "display.h"
#include "test_app.h"

static const char *TAG = "nanosoul";

void app_main(void)
{
    ESP_LOGI(TAG, "NanoSoul HW TEST boot — ESP32-P4, 屏控硬件自检");

    ESP_ERROR_CHECK(board_i2c1_init());       // 传感/电流总线 (SDA=IO20/SCL=IO21)
    ESP_ERROR_CHECK(board_i2c0_init());       // 触摸总线 (SDA=IO7/SCL=IO8)
    ESP_ERROR_CHECK(display_init());          // ST7701 MIPI-DSI + LVGL
    display_touch_init(board_i2c0_bus());     // FT6x36 → LVGL indev（探不到也继续）
    ESP_ERROR_CHECK(test_app_start());        // 电机/编码器/IMU/光照 + 触摸面板
}
