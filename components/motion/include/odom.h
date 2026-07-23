#pragma once
/*
 * odom — 三轮全向正运动学里程计（docs/14）。motion_ik 的解析逆（120° 均布）：
 *   vx = (2/3)·Σ(−sin aᵢ)·vᵢ    vy = (2/3)·Σ(cos aᵢ)·vᵢ    wz = Σvᵢ / (3R)
 * 纯函数（状态由调用方持有），集成在 wheelctrl 50Hz 任务里喂计数增量。
 * 口径：短距位移预算用（docs/12 approach_budget_cm），辊子打滑 ±8% 级误差，
 * 不做全局定位。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float wheel_r_mm;      /* 有效轮半径（滚动实测，docs/14 A3） */
    float body_r_mm;       /* 轮心到机器人中心距离               */
    float counts_per_rev;  /* 输出轴每圈编码器计数（28×118=3304）*/
} odom_geom_t;

typedef struct {
    float x_mm, y_mm;      /* odom 系位姿；th 不回卷（短程口径够用） */
    float th_rad;
} odom_pose_t;

/* 纯 FK：三轮切向量 v[3]（任意一致单位：mm 或 mm/s）→ body 系 (vx, vy, wz)。
 * out[0]=vx, out[1]=vy 同 v 的单位；out[2]=wz 为 rad（或 rad/s）。 */
void motion_fk(const float v[3], float body_r_mm, float out[3]);

/* 走一步：编码器计数增量（电机轴口径，带符号）→ 位姿积分（中点航向法）。 */
void odom_step(odom_pose_t *p, const int32_t dcount[3], const odom_geom_t *g);

void odom_reset(odom_pose_t *p);

#ifdef __cplusplus
}
#endif
