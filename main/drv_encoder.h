// 三路正交编码器（无 PCB 版 M0/M1/M2）PCNT ×4 解码。
// 引脚 docs/BOARD_MAPPING.md：M0 A/B=IO30/IO31 · M1 IO28/IO29 · M2 IO46/IO47。
// 开漏霍尔 → 启用内部上拉到 3V3（docs 硬性要求）。
#pragma once

#include "esp_err.h"

#define ENCODER_COUNT 3

// 商家：电机轴每转 7 个脉冲(7PPR)。正交 ×4 → 28 计数/电机轴圈。
// 故 encoder_rpm() 是【电机轴 RPM】；输出轴 RPM = 电机轴RPM ÷ 减速比。
// 要输出轴每圈计数：手转输出轴整一圈读 ΔENC 反校准。
#define ENC_COUNTS_PER_REV 28

esp_err_t encoders_init(void);

// 第 idx(0..2) 路累加计数（带符号，已处理 PCNT 溢出）。
int encoder_count(int idx);

// 第 idx 路转速（100ms 周期采样 + 死区 + EMA，带符号）。
float encoder_rpm(int idx);

// 第 idx 路 A/B 瞬时电平（诊断：慢转看两路是否都 0/1 跳）。
void encoder_raw_levels(int idx, int *a, int *b);
