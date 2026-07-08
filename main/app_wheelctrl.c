#include "app_wheelctrl.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "drv_encoder.h"
#include "drv_motor.h"
#include "motion.h"
#include "ns_config.h"
#include "odom.h"
#include "telemetry.h"

static const char *TAG = "wheelctrl";

#define WC_PERIOD_MS   20        /* 50Hz：满速 ~150 计数/拍，10% 速度 ~15 计数/拍 */

/* 积分项对输出的最大贡献（duty counts）。前馈担主力，积分只修 kv 标定残差
 * （120 duty ≈ 满速下 12% 的修正权限）；夹太松会在斜坡期灌满导致大超调。
 * 注意 pid_ctrl 的 max_integral 夹的是【误差累计和，rpm·拍】：换算 = 上限/ki。 */
#define WC_TRIM_I_MAX  120.0f

static pid_ctrl_parameter_t wc_pid_param(float kp, float ki, float kd)
{
    float max_sum = (ki > 1e-6f) ? WC_TRIM_I_MAX / ki : 1e9f;
    return (pid_ctrl_parameter_t){
        .kp = kp, .ki = ki, .kd = kd,
        .max_output = MOTOR_DUTY_MAX,
        .min_output = -MOTOR_DUTY_MAX,
        .max_integral = max_sum,
        .min_integral = -max_sum,
        .cal_type = PID_CAL_TYPE_POSITIONAL,
    };
}

/* ---------------- 纯控制器（单轮） ---------------- */

esp_err_t wc_state_init(wc_state_t *st, const wc_gains_t *g, float kp, float ki, float kd)
{
    (void)g;
    memset(st, 0, sizeof(*st));
    pid_ctrl_config_t cfg = { .init_param = wc_pid_param(kp, ki, kd) };
    return pid_new_control_block(&cfg, &st->pid);
}

void wc_state_deinit(wc_state_t *st)
{
    if (st->pid) {
        pid_del_control_block(st->pid);
        st->pid = NULL;
    }
}

void wc_reset(wc_state_t *st)
{
    st->sp = 0.0f;
    st->meas = 0.0f;
    if (st->pid) {
        pid_reset_ctrl_block(st->pid);
    }
}

float wc_step(wc_state_t *st, const wc_gains_t *g, float sp_target, float meas_inst, float dt_s)
{
    /* 死区看目标值（斜坡中间值低于阈值是正常的起步过程，不能据此清零卡死）；
     * 目标进死区时把斜坡目标改成 0，平滑滑到停。 */
    bool sp_small = fabsf(sp_target) < g->deadband_rpm;

    /* 设定值斜坡（每轮同速率限幅；意图级缓入缓出由原语层负责，这里是安全包络） */
    float dmax = g->ramp_rpm_per_s * dt_s;
    float d = (sp_small ? 0.0f : sp_target) - st->sp;
    if (d > dmax) d = dmax;
    if (d < -dmax) d = -dmax;
    st->sp += d;

    /* 测量 EMA（50Hz 差分的量化噪声，低速端 ±1 计数 ≈ ±107 rpm 电机轴） */
    st->meas += g->meas_alpha * (meas_inst - st->meas);

    if (sp_small && fabsf(st->sp) < g->deadband_rpm) {
        /* 稳定停下：零输出 + 清积分；meas 继续跟踪（滑行中重新起步不失真） */
        st->sp = 0.0f;
        if (st->pid) {
            pid_reset_ctrl_block(st->pid);
        }
        return 0.0f;
    }

    float trim = 0.0f;
    if (st->pid) {
        pid_compute(st->pid, st->sp - st->meas, &trim);
    }
    float out = g->ks * (st->sp > 0 ? 1.0f : -1.0f) + g->kv * st->sp + trim;
    if (out > MOTOR_DUTY_MAX) out = MOTOR_DUTY_MAX;
    if (out < -MOTOR_DUTY_MAX) out = -MOTOR_DUTY_MAX;
    return out;
}

/* ---------------- 三轮任务 ---------------- */

static wc_gains_t s_gains[MOTOR_COUNT];
static wc_state_t s_wc[MOTOR_COUNT];
static float      s_rpm_max = 15000.0f;

/* 目标设定值：apply/override 写、控制任务读。都是任务上下文（motion 的
 * esp_timer 回调跑在 esp_timer 任务里），自旋锁护 12 字节拷贝足够。 */
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static float         s_target[MOTOR_COUNT];
static volatile bool s_active;          /* 有驱动权才碰 motor_set */
static int64_t       s_override_end_us; /* >now 时 s_target 来自 override */

static bool           s_running;
static bool           s_drive;                /* closed_loop：false=只测量+里程计 */

/* 里程计：任务独占写；reset 请求用标志位跨任务传递（避免半步撕裂） */
static odom_geom_t   s_geom;
static odom_pose_t   s_pose;
static volatile bool s_odom_reset_req;

void wheel_ctrl_odom_reset(void) { s_odom_reset_req = true; }
bool wheel_ctrl_closed_loop(void) { return s_running && s_drive; }

void wheel_ctrl_apply(const int16_t cmd[3])
{
    if (!s_drive) {
        return;   /* 只测量模式不该被注册为 apply 桥；防御 */
    }
    float scale = s_rpm_max / (float)MOTION_DUTY_MAX;
    portENTER_CRITICAL(&s_mux);
    if (esp_timer_get_time() >= s_override_end_us) {   /* override 期间 motion 让位 */
        for (int i = 0; i < MOTOR_COUNT; i++) {
            s_target[i] = (float)cmd[i] * scale;
        }
        s_active = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

void wheel_ctrl_override_sp(const float sp_rpm[3], uint32_t ms)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MOTOR_COUNT; i++) {
        float v = sp_rpm[i];
        if (v > s_rpm_max) v = s_rpm_max;
        if (v < -s_rpm_max) v = -s_rpm_max;
        s_target[i] = v;
    }
    s_override_end_us = esp_timer_get_time() + (int64_t)ms * 1000;
    s_active = true;
    portEXIT_CRITICAL(&s_mux);
}

void wheel_ctrl_halt(void)
{
    portENTER_CRITICAL(&s_mux);
    memset((void *)s_target, 0, sizeof(s_target));
    s_override_end_us = 0;
    s_active = false;
    portEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < MOTOR_COUNT; i++) {
        wc_reset(&s_wc[i]);
    }
    if (s_running) {
        motor_stop_all();
    }
}

void wheel_ctrl_pid_set(float kp, float ki, float kd)
{
    pid_ctrl_parameter_t p = wc_pid_param(kp, ki, kd);
    for (int i = 0; i < MOTOR_COUNT; i++) {
        if (s_wc[i].pid) {
            pid_update_parameters(s_wc[i].pid, &p);
        }
    }
    ESP_LOGI(TAG, "pid_set kp=%.4f ki=%.4f kd=%.4f", kp, ki, kd);
}

bool wheel_ctrl_running(void) { return s_running; }

static void wheelctrl_task(void *arg)
{
    (void)arg;
    int last_count[MOTOR_COUNT];
    for (int i = 0; i < MOTOR_COUNT; i++) {
        last_count[i] = encoder_count(i);
    }
    int64_t last_us = esp_timer_get_time();
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(WC_PERIOD_MS));

        /* 真实 dt 算转速（掉拍时 rpm 不毛刺）；夹取防除零/防长挂恢复冲击 */
        int64_t now_us = esp_timer_get_time();
        float dt_s = (float)(now_us - last_us) * 1e-6f;
        last_us = now_us;
        if (dt_s < 0.010f) dt_s = 0.010f;
        if (dt_s > 0.040f) dt_s = 0.040f;

        float   meas_inst[MOTOR_COUNT];
        int32_t dcount[MOTOR_COUNT];
        for (int i = 0; i < MOTOR_COUNT; i++) {
            int c = encoder_count(i);
            dcount[i] = c - last_count[i];
            meas_inst[i] = (float)dcount[i] / (float)ENC_COUNTS_PER_REV
                           * (60.0f / dt_s);   /* 电机轴 rpm */
            last_count[i] = c;
        }

        /* 里程计：开环/闭环都积分（校准和位移预算都要用） */
        if (s_odom_reset_req) {
            s_odom_reset_req = false;
            odom_reset(&s_pose);
        }
        odom_step(&s_pose, dcount, &s_geom);
        tel_odom_t od = { .x_mm = s_pose.x_mm, .y_mm = s_pose.y_mm, .th_rad = s_pose.th_rad };
        telemetry_set_odom(&od);

        float target[MOTOR_COUNT];
        bool active;
        portENTER_CRITICAL(&s_mux);
        if (s_override_end_us && now_us >= s_override_end_us) {
            /* override 到期自动归零（等 motion 下一拍 apply 接管） */
            s_override_end_us = 0;
            memset((void *)s_target, 0, sizeof(s_target));
        }
        memcpy(target, (const void *)s_target, sizeof(target));
        active = s_active;
        portEXIT_CRITICAL(&s_mux);

        float sp_now[MOTOR_COUNT] = { 0 };
        if (!s_drive || !active) {
            telemetry_set_wheel_sp(sp_now);
            continue;   /* 只测量模式 / halt 后：别碰电机（motor_test 可能在开） */
        }

        if (!motors_enabled()) {
            /* STBY 被急停/堵转 FSM 拉低：冻结并清积分，重使能后干净起步 */
            for (int i = 0; i < MOTOR_COUNT; i++) {
                wc_reset(&s_wc[i]);
            }
            telemetry_set_wheel_sp(sp_now);
            continue;
        }

        for (int i = 0; i < MOTOR_COUNT; i++) {
            float duty = wc_step(&s_wc[i], &s_gains[i], target[i], meas_inst[i], dt_s);
            sp_now[i] = s_wc[i].sp;
            int d = (int)lroundf(duty);
            motor_dir_t dir = d > 0 ? MOTOR_FORWARD : (d < 0 ? MOTOR_REVERSE : MOTOR_COAST);
            motor_set(i, dir, d > 0 ? d : -d);
        }
        telemetry_set_wheel_sp(sp_now);
    }
}

esp_err_t wheel_ctrl_start(void)
{
    if (s_running) {
        return ESP_OK;
    }
    const ns_calib_cfg_t *c = &ns_config_get()->motion.calib;
    int ramp_ms = ns_config_get()->move.ramp_ms;
    if (ramp_ms < 50) ramp_ms = 50;   /* docs/12：斜坡 ≥100ms；50 兜底防除零 */

    s_drive = c->closed_loop;
    s_geom = (odom_geom_t){
        .wheel_r_mm = c->wheel_d_mm * 0.5f,
        .body_r_mm = c->body_r_mm,
        .counts_per_rev = (float)(ENC_COUNTS_PER_REV * ENC_GEAR_RATIO),
    };
    odom_reset(&s_pose);

    s_rpm_max = (c->rpm_max > 1000.0f) ? c->rpm_max : 15000.0f;
    for (int i = 0; i < MOTOR_COUNT; i++) {
        s_gains[i] = (wc_gains_t){
            .ks = c->ks[i],
            .kv = c->kv[i],
            .deadband_rpm = s_rpm_max * c->sp_deadband_pct / 100.0f,
            .meas_alpha = 0.4f,
            .ramp_rpm_per_s = s_rpm_max * 1000.0f / (float)ramp_ms,
        };
        ESP_RETURN_ON_ERROR(wc_state_init(&s_wc[i], &s_gains[i], c->pid_kp, c->pid_ki, c->pid_kd),
                            TAG, "pid block %d", i);
    }

    BaseType_t ok = xTaskCreatePinnedToCore(wheelctrl_task, "wheelctrl", 4096, NULL, 5, NULL, 0);
    if (ok != pdPASS) {
        for (int i = 0; i < MOTOR_COUNT; i++) {
            wc_state_deinit(&s_wc[i]);
        }
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    ESP_LOGI(TAG, "%s up @%dHz: rpm_max=%.0f ks=[%.0f %.0f %.0f] kv=[%.4f %.4f %.4f] "
             "pid=[%.3f %.3f %.3f] deadband=%.0frpm ramp=%dms",
             s_drive ? "closed loop" : "measure-only (odom)",
             1000 / WC_PERIOD_MS, s_rpm_max,
             c->ks[0], c->ks[1], c->ks[2], c->kv[0], c->kv[1], c->kv[2],
             c->pid_kp, c->pid_ki, c->pid_kd, s_gains[0].deadband_rpm, ramp_ms);
    return ESP_OK;
}
