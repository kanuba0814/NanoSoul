// 单路正交编码器（无 PCB 版 M2：A=IO46 B=IO47）PCNT ×4 解码。
// 开漏霍尔 → 启用内部上拉到 3V3（docs 硬性要求）。
#pragma once

#include "esp_err.h"

// 商家：电机轴每转 7 个脉冲(7 PPR)。正交 ×4 → 28 计数/电机轴圈。
// 故 encoder_rpm() 得到的是【电机轴 RPM】；输出轴 RPM = 电机轴RPM ÷ 减速比。
// 要输出轴每圈计数：手转输出轴整一圈读 ΔENC 反校准，填到这里最准。
#define ENC_COUNTS_PER_REV 28

esp_err_t encoder_init(void);

// 累加计数（已处理 PCNT 16-bit 溢出；正反转带符号）。
int encoder_count(void);

// 自上次调用以来的转速（带符号，+ 为计数增加方向）。固定节奏调用。
float encoder_rpm(void);

// 瞬时读 A/B 两路电平（诊断用：慢转电机看两路是否都在 0/1 跳；某路恒定=那路没接好）。
void encoder_raw_levels(int *a, int *b);
