#include "drv_motor.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "motor";

#define PWM_MODE     LEDC_LOW_SPEED_MODE   // P4 只有低速域
#define PWM_TIMER    LEDC_TIMER_0          // 三路共用一个 20kHz 定时器
#define PWM_RES      LEDC_TIMER_10_BIT
#define PWM_FREQ_HZ  20000                 // ~20kHz，超声、避开可闻噪声

typedef struct {
    gpio_num_t pwm, in1, in2, stby;
    ledc_channel_t ch;
} motor_cfg_t;

// 引脚真值源 docs/BOARD_MAPPING.md（杜邦接，可改；改了同步该文件）。
// STBY 两片分开：M0/M1 桥=IO51，M2 桥=IO52（实物杜邦不够没并到一起）。
static const motor_cfg_t s_motors[MOTOR_COUNT] = {
    { GPIO_NUM_2,  GPIO_NUM_3,  GPIO_NUM_4,  GPIO_NUM_51, LEDC_CHANNEL_0 },  // M0
    { GPIO_NUM_5,  GPIO_NUM_24, GPIO_NUM_25, GPIO_NUM_51, LEDC_CHANNEL_1 },  // M1
    { GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_52, LEDC_CHANNEL_2 },  // M2
};

esp_err_t motors_init(void)
{
    // 所有 IN1/IN2 + 所有 STBY 配成输出（位掩码自动去重）
    uint64_t mask = 0;
    for (int i = 0; i < MOTOR_COUNT; i++) {
        mask |= (1ULL << s_motors[i].in1) | (1ULL << s_motors[i].in2) | (1ULL << s_motors[i].stby);
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");

    // 1 个共享 20kHz 定时器
    ledc_timer_config_t tcfg = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer");

    for (int i = 0; i < MOTOR_COUNT; i++) {
        gpio_set_level(s_motors[i].in1, 0);
        gpio_set_level(s_motors[i].in2, 0);
        gpio_set_level(s_motors[i].stby, 0);   // 上电先禁用，等显式 enable
        ledc_channel_config_t ccfg = {
            .gpio_num = s_motors[i].pwm,
            .speed_mode = PWM_MODE,
            .channel = s_motors[i].ch,
            .timer_sel = PWM_TIMER,
            .duty = 0,
            .hpoint = 0,
            .intr_type = LEDC_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc ch");
        ESP_LOGI(TAG, "M%d: PWM=IO%d IN1=IO%d IN2=IO%d STBY=IO%d ch%d", i,
                 s_motors[i].pwm, s_motors[i].in1, s_motors[i].in2, s_motors[i].stby, s_motors[i].ch);
    }
    ESP_LOGI(TAG, "%d motors @%dHz", MOTOR_COUNT, PWM_FREQ_HZ);
    return ESP_OK;
}

void motors_enable(bool en)
{
    // 两片 STBY 都拉（IO51 会被写两次，无害）
    for (int i = 0; i < MOTOR_COUNT; i++) {
        gpio_set_level(s_motors[i].stby, en ? 1 : 0);
    }
}

void motor_set(int idx, motor_dir_t dir, int duty)
{
    if (idx < 0 || idx >= MOTOR_COUNT) {
        return;
    }
    if (duty < 0) duty = 0;
    if (duty > MOTOR_DUTY_MAX) duty = MOTOR_DUTY_MAX;

    const motor_cfg_t *m = &s_motors[idx];
    int in1 = 0, in2 = 0;
    switch (dir) {
        case MOTOR_FORWARD: in1 = 1; in2 = 0; break;
        case MOTOR_REVERSE: in1 = 0; in2 = 1; break;
        case MOTOR_BRAKE:   in1 = 1; in2 = 1; break;
        case MOTOR_COAST:   default: in1 = 0; in2 = 0; duty = 0; break;
    }
    gpio_set_level(m->in1, in1);
    gpio_set_level(m->in2, in2);
    ledc_set_duty(PWM_MODE, m->ch, duty);
    ledc_update_duty(PWM_MODE, m->ch);
}

void motor_stop_all(void)
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_set(i, MOTOR_COAST, 0);
    }
}
