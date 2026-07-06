// 最简屏控硬件自检：屏上按钮点一下驱动对应电机一段有界 FWD→REV 脉冲，
// 实时刷新编码器/电流/IMU/光照读数 + I²C1 扫描。全部跑在 P4 本地，无上位机。
// 前置：display_init() + display_touch_init() 已调用（屏 + LVGL + 触摸已起）；
//       board_i2c1_init() 已调用（电流/IMU/光照共总线）。
#pragma once

#include "esp_err.h"

// 初始化电机/编码器/电流/IMU/光照 → 建触摸面板 → 起采样/驱动任务。
esp_err_t test_app_start(void);
