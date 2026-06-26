// 单路正交编码器（无 PCB 版 M2：A=IO46 B=IO47）PCNT ×4 解码。
// 开漏霍尔 → 启用内部上拉到 3V3（docs 硬性要求）。
#pragma once

#include "esp_err.h"

// docs 推算：7PPR × 118 减速 = 826/相，正交 ×4 = 3304 计数/圈（输出轴）。
// 上板后可用本测试反校准：手转一整圈看 encoder_count() 增量。
#define ENC_COUNTS_PER_REV 3304

esp_err_t encoder_init(void);

// 累加计数（已处理 PCNT 16-bit 溢出；正反转带符号）。
int encoder_count(void);

// 自上次调用以来的转速（带符号，+ 为计数增加方向）。固定节奏调用。
float encoder_rpm(void);
