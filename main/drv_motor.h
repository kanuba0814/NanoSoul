// 三路 TB6612 桥（无 PCB 版 M0/M1/M2）驱动：LEDC 20kHz PWM + IN1/IN2 方向 + STBY。
// 引脚 docs/BOARD_MAPPING.md：M0 PWM=IO2/IN1=IO3/IN2=IO4 · M1 IO5/IO24/IO25 · M2 IO26/IO27/IO32。
// STBY 两片分开接（杜邦不够没并）：M0/M1 桥=IO51，M2 桥=IO52；enable 时两片一起拉高。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define MOTOR_COUNT     3
#define MOTOR_DUTY_MAX  ((1 << 10) - 1)   // LEDC 10-bit

typedef enum {
    MOTOR_COAST = 0,  // IN1=0 IN2=0：自由滑行（停）
    MOTOR_FORWARD,    // IN1=1 IN2=0
    MOTOR_REVERSE,    // IN1=0 IN2=1
    MOTOR_BRAKE,      // IN1=1 IN2=1：短接刹车
} motor_dir_t;

// 配 3 路 LEDC + IN1/IN2 + 共用 STBY；初始 STBY=低（禁用），占空 0。
esp_err_t motors_init(void);

// 共用 STBY：true=使能，false=急停（拉低，三路输出全关）。
void motors_enable(bool en);

// 设第 idx(0..2) 路方向 + 占空（0..MOTOR_DUTY_MAX，越界自动夹）。
void motor_set(int idx, motor_dir_t dir, int duty);

// 三路全部 COAST。
void motor_stop_all(void);
