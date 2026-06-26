// 单路 TB6612 桥（无 PCB 版 M2）驱动：LEDC 20kHz PWM + IN1/IN2 方向 + 共用 STBY。
// 引脚见 docs/BOARD_MAPPING.md M2 行：PWM=IO26 IN1=IO27 IN2=IO32 STBY=IO52。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// LEDC 10-bit → 占空范围 [0, MOTOR_DUTY_MAX]。
#define MOTOR_DUTY_MAX ((1 << 10) - 1)

typedef enum {
    MOTOR_COAST = 0,  // IN1=0 IN2=0：自由滑行（停）
    MOTOR_FORWARD,    // IN1=1 IN2=0
    MOTOR_REVERSE,    // IN1=0 IN2=1
    MOTOR_BRAKE,      // IN1=1 IN2=1：短接刹车
} motor_dir_t;

// 配置 LEDC + IN1/IN2/STBY；初始 STBY=低（禁用/急停态），占空 0。
esp_err_t motor_init(void);

// STBY：true=使能，false=急停（拉低，输出全关）。
void motor_enable(bool en);

// 设方向 + 占空（0..MOTOR_DUTY_MAX，越界自动夹）。
void motor_set(motor_dir_t dir, int duty);

// 等价 motor_set(MOTOR_COAST, 0)。
void motor_stop(void);
