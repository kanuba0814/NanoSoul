// M2 单桥自检：自动循环驱动电机 + 屏上实时显示动作/占空/编码器/转速/电流。
// 前置：display_init() 已调用（屏 + LVGL 已起）。
#pragma once

#include "esp_err.h"

// 初始化电机/编码器/电流 → 建 LVGL 状态面板 → 起自动循环任务。
esp_err_t test_app_start(void);
