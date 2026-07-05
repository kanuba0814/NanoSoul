#include "drv_encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "encoder";

#define PCNT_HIGH_LIMIT  30000   // ≤ int16 上限；到限自动累加并清零
#define PCNT_LOW_LIMIT  -30000
#define GLITCH_NS        1000    // 1µs 去抖，远小于最高转速边沿间隔
#define RPM_SAMPLE_US   100000   // 100ms 固定采样窗
#define RPM_EMA_ALPHA   0.4f     // 指数平滑
#define RPM_DEADBAND    1        // |Δ计数| ≤ 此值视为静止 → 消零飘

typedef struct { gpio_num_t a, b; } enc_pins_t;

// 引脚真值源 docs/BOARD_MAPPING.md
static const enc_pins_t s_pins[ENCODER_COUNT] = {
    { GPIO_NUM_30, GPIO_NUM_31 },  // M0
    { GPIO_NUM_28, GPIO_NUM_29 },  // M1
    { GPIO_NUM_46, GPIO_NUM_47 },  // M2
};

static pcnt_unit_handle_t s_unit[ENCODER_COUNT] = { 0 };
static esp_timer_handle_t s_rpm_timer = NULL;
static volatile float s_rpm[ENCODER_COUNT] = { 0 };   // 周期采样+平滑后的电机轴转速
static int s_prev_count[ENCODER_COUNT] = { 0 };
static int64_t s_prev_us = 0;

// 100ms 周期：一次采三路，固定窗口算瞬时转速 → 死区滤零飘 → EMA 平滑。
static void rpm_sample_cb(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - s_prev_us) / 1e6f;
    s_prev_us = now;
    if (dt <= 0.0f) {
        return;
    }
    for (int i = 0; i < ENCODER_COUNT; i++) {
        int c = 0;
        pcnt_unit_get_count(s_unit[i], &c);
        int dc = c - s_prev_count[i];
        s_prev_count[i] = c;
        float inst = 0.0f;
        if (dc > RPM_DEADBAND || dc < -RPM_DEADBAND) {
            inst = ((float)dc / (float)ENC_COUNTS_PER_REV) * (60.0f / dt);
        }
        s_rpm[i] = s_rpm[i] * (1.0f - RPM_EMA_ALPHA) + inst * RPM_EMA_ALPHA;
    }
}

static esp_err_t init_one(int i)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
        .flags.accum_count = true,   // 到限把计数累加进 get_count，防溢出
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_unit[i]), TAG, "new unit");

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = GLITCH_NS };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_unit[i], &filter), TAG, "filter");

    // 通道 A：边沿采 A、电平看 B；通道 B：边沿采 B、电平看 A → 标准 ×4 正交
    pcnt_chan_config_t cha_cfg = { .edge_gpio_num = s_pins[i].a, .level_gpio_num = s_pins[i].b };
    pcnt_channel_handle_t cha = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit[i], &cha_cfg, &cha), TAG, "chan a");
    pcnt_chan_config_t chb_cfg = { .edge_gpio_num = s_pins[i].b, .level_gpio_num = s_pins[i].a };
    pcnt_channel_handle_t chb = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit[i], &chb_cfg, &chb), TAG, "chan b");

    // ★ 两个通道 level_action 都必须 (KEEP, INVERSE)，否则 B 把 A 抵消→计数在 0/1 弹
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(cha,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "a edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(cha,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "a level");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chb,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE), TAG, "b edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chb,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "b level");

    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit[i], PCNT_HIGH_LIMIT), TAG, "wp hi");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit[i], PCNT_LOW_LIMIT), TAG, "wp lo");

    // 开漏霍尔：内部上拉到 3V3
    gpio_set_pull_mode(s_pins[i].a, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(s_pins[i].b, GPIO_PULLUP_ONLY);

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit[i]), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit[i]), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_unit[i]), TAG, "start");
    ESP_LOGI(TAG, "M%d encoder: A=IO%d B=IO%d", i, s_pins[i].a, s_pins[i].b);
    return ESP_OK;
}

esp_err_t encoders_init(void)
{
    for (int i = 0; i < ENCODER_COUNT; i++) {
        ESP_RETURN_ON_ERROR(init_one(i), TAG, "enc init");
        s_prev_count[i] = 0;
    }
    s_prev_us = esp_timer_get_time();

    const esp_timer_create_args_t rpm_timer_args = { .callback = rpm_sample_cb, .name = "enc_rpm" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&rpm_timer_args, &s_rpm_timer), TAG, "rpm timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_rpm_timer, RPM_SAMPLE_US), TAG, "rpm start");

    ESP_LOGI(TAG, "%d encoders, %d 计数/圈, RPM 采样 %dms",
             ENCODER_COUNT, ENC_COUNTS_PER_REV, (int)(RPM_SAMPLE_US / 1000));
    return ESP_OK;
}

int encoder_count(int idx)
{
    int c = 0;
    if (idx >= 0 && idx < ENCODER_COUNT && s_unit[idx]) {
        pcnt_unit_get_count(s_unit[idx], &c);
    }
    return c;
}

float encoder_rpm(int idx)
{
    return (idx >= 0 && idx < ENCODER_COUNT) ? s_rpm[idx] : 0.0f;
}

void encoder_raw_levels(int idx, int *a, int *b)
{
    if (idx < 0 || idx >= ENCODER_COUNT) {
        if (a) *a = 0;
        if (b) *b = 0;
        return;
    }
    if (a) *a = gpio_get_level(s_pins[idx].a);
    if (b) *b = gpio_get_level(s_pins[idx].b);
}
