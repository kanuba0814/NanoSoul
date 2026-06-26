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

#define RPM_SAMPLE_US   100000   // 100ms 固定采样窗：稳定 dt，不受 UI 时序抖动影响
#define RPM_EMA_ALPHA   0.4f     // 指数平滑系数（越小越稳、越大越跟手）
#define RPM_DEADBAND    1        // |Δ计数| ≤ 此值视为静止 → 消零飘

static pcnt_unit_handle_t s_unit = NULL;
static esp_timer_handle_t s_rpm_timer = NULL;
static volatile float s_rpm = 0.0f;   // 周期采样 + 平滑后的电机轴转速（带符号）
static int s_prev_count = 0;
static int64_t s_prev_us = 0;

// 100ms 周期：固定窗口算瞬时转速 → 死区滤零飘 → EMA 平滑。PCNT 已在硬件计数，这里只做时基。
static void rpm_sample_cb(void *arg)
{
    (void)arg;
    int c = 0;
    pcnt_unit_get_count(s_unit, &c);
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - s_prev_us) / 1e6f;
    int dc = c - s_prev_count;
    s_prev_count = c;
    s_prev_us = now;
    if (dt <= 0.0f) {
        return;
    }
    float inst = 0.0f;
    if (dc > RPM_DEADBAND || dc < -RPM_DEADBAND) {
        inst = ((float)dc / (float)ENC_COUNTS_PER_REV) * (60.0f / dt);
    }
    s_rpm = s_rpm * (1.0f - RPM_EMA_ALPHA) + inst * RPM_EMA_ALPHA;
}

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
    // ★ 两个通道的 level_action 都必须是 (KEEP, INVERSE)。
    // 之前 B 写成 (INVERSE, KEEP) → B 的计数把 A 的抵消掉 → ENC 在 0/1 来回弹。
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chb,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "b level");

    // accum_count 需配高/低限观察点
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, PCNT_HIGH_LIMIT), TAG, "wp hi");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, PCNT_LOW_LIMIT), TAG, "wp lo");

    // 开漏霍尔：启用内部上拉到 3V3（pcnt 把脚配成输入后再设上拉）
    gpio_set_pull_mode(ENC_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(ENC_B, GPIO_PULLUP_ONLY);

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_unit), TAG, "start");

    s_prev_us = esp_timer_get_time();
    s_prev_count = 0;
    const esp_timer_create_args_t rpm_timer_args = {
        .callback = rpm_sample_cb,
        .name = "enc_rpm",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&rpm_timer_args, &s_rpm_timer), TAG, "rpm timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_rpm_timer, RPM_SAMPLE_US), TAG, "rpm start");

    ESP_LOGI(TAG, "M2 encoder init: A=IO%d B=IO%d, %d 计数/圈, RPM 采样 %dms",
             ENC_A, ENC_B, ENC_COUNTS_PER_REV, (int)(RPM_SAMPLE_US / 1000));
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
    return s_rpm;   // 100ms 周期采样 + 死区 + EMA 得出（电机轴 RPM，带符号）
}

void encoder_raw_levels(int *a, int *b)
{
    // PCNT 走 GPIO 矩阵输入，pad 电平仍可经 gpio_get_level 读到。
    if (a) *a = gpio_get_level(ENC_A);
    if (b) *b = gpio_get_level(ENC_B);
}
