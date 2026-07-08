// 每轮转速闭环（docs/14）：前馈 kS/kV + pid_ctrl PI 修正，50Hz 自差分编码器计数。
// motion 的 ±1023 归一化指令在这里解释为转速设定值比例：sp = cmd/1023 × rpm_max。
// closed_loop=false 时不要启动本模块——app_face 直接注册旧的 duty 直通桥。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pid_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 纯控制器（单轮，供 sim 自检；无硬件依赖，PID 状态在 pid_ctrl 块里）---- */

typedef struct {
    float ks;             /* 静摩擦前馈，duty counts（按 sign(sp) 加） */
    float kv;             /* 速度前馈，duty / 电机轴 rpm               */
    float deadband_rpm;   /* |sp| 低于此值 → 输出 0 + 复位（防零点抖鸣）*/
    float meas_alpha;     /* 测量 EMA 系数（新样本权重）               */
    float ramp_rpm_per_s; /* 设定值斜坡限速（docs/12 加速度包络）      */
} wc_gains_t;

typedef struct {
    pid_ctrl_block_handle_t pid;
    float sp;             /* 斜坡后的当前设定值，电机轴 rpm */
    float meas;           /* EMA 后的测量值，电机轴 rpm     */
} wc_state_t;

/* 建 PID 块（positional，输出 ±1023、积分限幅）。kp/ki/kd 是每拍口径（dt 已含在增益里）。 */
esp_err_t wc_state_init(wc_state_t *st, const wc_gains_t *g, float kp, float ki, float kd);
void      wc_state_deinit(wc_state_t *st);

/* 走一拍：斜坡 → EMA → 死区 or 前馈+PI。返回带符号 duty ∈ [-1023, 1023]。 */
float wc_step(wc_state_t *st, const wc_gains_t *g, float sp_target, float meas_inst, float dt_s);

/* 清设定值/测量/积分（急停、STBY 拉低、驱动权移交时用）。 */
void wc_reset(wc_state_t *st);

/* ---- 三轮闭环任务（读 ns_config motion.calib，50Hz 驱动 drv_motor）----
 * closed_loop=false 时也可启动：只做 50Hz 测速差分 + 里程计积分，不碰电机。 */

esp_err_t wheel_ctrl_start(void);

/* 任务在跑且 closed_loop 配置开着（= wheel_ctrl_apply 应作为 motion 桥）。 */
bool wheel_ctrl_closed_loop(void);

/* 里程计清零（下一拍生效；校准 A3 与位移预算结算用）。 */
void wheel_ctrl_odom_reset(void);

/* motion 的 apply 桥（闭环版）：cmd ±1023 → 设定值。收到指令即认领驱动权。 */
void wheel_ctrl_apply(const int16_t cmd[3]);

/* 调参用：直接给三轮设定值（电机轴 rpm），ms 后自动归零。同样认领驱动权。 */
void wheel_ctrl_override_sp(const float sp_rpm[3], uint32_t ms);

/* 放弃驱动权：清设定值+积分，轮子 COAST。下一次 apply/override 自动重新认领。 */
void wheel_ctrl_halt(void);

/* 在线改 PI 增益（三轮同套；wheel_sp 阶跃 + pid_set 命令配套用）。 */
void wheel_ctrl_pid_set(float kp, float ki, float kd);

bool wheel_ctrl_running(void);

#ifdef __cplusplus
}
#endif
