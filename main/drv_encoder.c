#include "drv_encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "encoder";

#define ENC_A           GPIO_NUM_46
#define ENC_B           GPIO_NUM_47
#define PCNT_HIGH_LIMIT  30000   // ≤ int16 上限；到限自动累加并清零
#define PCNT_LOW_LIMIT  -30000
#define GLITCH_NS        1000    // 1µs 去抖，远小于最高转速的边沿间隔

static pcnt_unit_handle_t s_unit = NULL;
static int s_last_count = 0;
static int64_t s_last_us = 0;

esp_err_t encoder_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
        .flags.accum_count = true,   // 到高/低限时把计数累加进 get_count，防溢出
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_unit), TAG, "new unit");

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = GLITCH_NS };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_unit, &filter), TAG, "filter");

    // 通道 A：边沿采 A、电平看 B；通道 B：边沿采 B、电平看 A → 标准 ×4 正交
    pcnt_chan_config_t cha_cfg = { .edge_gpio_num = ENC_A, .level_gpio_num = ENC_B };
    pcnt_channel_handle_t cha = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &cha_cfg, &cha), TAG, "chan a");
    pcnt_chan_config_t chb_cfg = { .edge_gpio_num = ENC_B, .level_gpio_num = ENC_A };
    pcnt_channel_handle_t chb = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &chb_cfg, &chb), TAG, "chan b");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(cha,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "a edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(cha,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "a level");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chb,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE), TAG, "b edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chb,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP), TAG, "b level");

    // accum_count 需配高/低限观察点
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, PCNT_HIGH_LIMIT), TAG, "wp hi");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, PCNT_LOW_LIMIT), TAG, "wp lo");

    // 开漏霍尔：启用内部上拉到 3V3（pcnt 把脚配成输入后再设上拉）
    gpio_set_pull_mode(ENC_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(ENC_B, GPIO_PULLUP_ONLY);

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_unit), TAG, "start");

    s_last_us = esp_timer_get_time();
    s_last_count = 0;
    ESP_LOGI(TAG, "M2 encoder init: A=IO%d B=IO%d, %d 计数/圈", ENC_A, ENC_B, ENC_COUNTS_PER_REV);
    return ESP_OK;
}

int encoder_count(void)
{
    int c = 0;
    if (s_unit) {
        pcnt_unit_get_count(s_unit, &c);
    }
    return c;
}

float encoder_rpm(void)
{
    int c = encoder_count();
    int64_t now = esp_timer_get_time();
    float dt = (now - s_last_us) / 1e6f;
    int dcount = c - s_last_count;
    s_last_count = c;
    s_last_us = now;
    if (dt <= 0.0f) {
        return 0.0f;
    }
    return ((float)dcount / (float)ENC_COUNTS_PER_REV) * (60.0f / dt);
}
