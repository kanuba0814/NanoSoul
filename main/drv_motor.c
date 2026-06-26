#include "drv_motor.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "motor";

// —— M2 引脚（无 PCB 版，杜邦接，可改；改了同步 docs/BOARD_MAPPING.md）——
#define M_PWM   GPIO_NUM_26
#define M_IN1   GPIO_NUM_27
#define M_IN2   GPIO_NUM_32
#define M_STBY  GPIO_NUM_52

#define PWM_MODE     LEDC_LOW_SPEED_MODE   // P4 只有低速域
#define PWM_TIMER    LEDC_TIMER_0
#define PWM_CHANNEL  LEDC_CHANNEL_0
#define PWM_RES      LEDC_TIMER_10_BIT
#define PWM_FREQ_HZ  20000                 // ~20kHz，超声、避开可闻噪声

esp_err_t motor_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << M_IN1) | (1ULL << M_IN2) | (1ULL << M_STBY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    gpio_set_level(M_IN1, 0);
    gpio_set_level(M_IN2, 0);
    gpio_set_level(M_STBY, 0);   // 上电先禁用，等显式 enable

    ledc_timer_config_t tcfg = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer");

    ledc_channel_config_t ccfg = {
        .gpio_num = M_PWM,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc ch");

    ESP_LOGI(TAG, "M2 init: PWM=IO%d IN1=IO%d IN2=IO%d STBY=IO%d @%dHz",
             M_PWM, M_IN1, M_IN2, M_STBY, PWM_FREQ_HZ);
    return ESP_OK;
}

void motor_enable(bool en)
{
    gpio_set_level(M_STBY, en ? 1 : 0);
}

void motor_set(motor_dir_t dir, int duty)
{
    if (duty < 0) duty = 0;
    if (duty > MOTOR_DUTY_MAX) duty = MOTOR_DUTY_MAX;

    int in1 = 0, in2 = 0;
    switch (dir) {
        case MOTOR_FORWARD: in1 = 1; in2 = 0; break;
        case MOTOR_REVERSE: in1 = 0; in2 = 1; break;
        case MOTOR_BRAKE:   in1 = 1; in2 = 1; break;
        case MOTOR_COAST:   default: in1 = 0; in2 = 0; duty = 0; break;
    }
    gpio_set_level(M_IN1, in1);
    gpio_set_level(M_IN2, in2);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

void motor_stop(void)
{
    motor_set(MOTOR_COAST, 0);
}
