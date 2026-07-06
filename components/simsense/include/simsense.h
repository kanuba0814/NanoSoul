#pragma once
/*
 * simsense — 测试模式的传感覆盖/注入层（docs/13）。
 *
 * 覆盖发生在各传感生产者边界（imu accel / light lux / app_sense 电流&转速 /
 * vision face），覆盖值随后走**正常全链路**：真实检测器 → 事件总线 → soul →
 * telemetry。核心代码对覆盖零感知，测的是真反应逻辑而非旁路仿真。
 *
 * 非测试场景下整表恒空，override_apply 一次原子读即返回 false，开销可忽略；
 * 所以生产者可以无条件调用它。线程安全（内部互斥）。每通道可带 TTL，惰性过期。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry.h"   /* tel_face_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OVR_LUX = 0,    /* 1 值: lx                                  */
    OVR_ACCEL,      /* 3 值: m/s^2 (x,y,z)                       */
    OVR_GYRO,       /* 3 值: dps — 预留，当前 IMU 驱动不读 gyro   */
    OVR_CURRENT_A,  /* 1 值: A（正=放电）                         */
    OVR_ENC_RPM,    /* 3 值: 电机轴 rpm (M0,M1,M2)                */
    OVR_FACE,       /* 5 值: present(0/1),cx,cy,area_ratio,frontal */
    OVR_CH_MAX,
} ovr_ch_t;

/* 每通道值数（供参数校验）。 */
size_t ovr_ch_len(ovr_ch_t ch);

/* 建互斥锁；幂等，可安全多次调用。 */
esp_err_t simsense_init(void);

/* 设一个通道的覆盖值。n 必须等于 ovr_ch_len(ch)。ttl_ms=0 表示常驻。 */
esp_err_t override_set(ovr_ch_t ch, const float *v, size_t n, uint32_t ttl_ms);
void      override_clear(ovr_ch_t ch);
void      override_clear_all(void);

/* 生产者边界调用：通道激活且未过期时，把覆盖值写进 inout[0..n-1] 并返回 true；
 * 否则不动 inout、返回 false。TTL 到期在此惰性清除。n 必须匹配通道值数。 */
bool override_apply(ovr_ch_t ch, float *inout, size_t n);

/* OVR_FACE 特型：把 5 个覆盖值填进 tel_face_t（present/cx/cy/area_ratio/
 * frontal_score，ts_ms 置当前时间保新鲜）。返回是否被覆盖。 */
bool override_face_apply(tel_face_t *f);

/* bit i = 通道 i 当前激活（供 sense 帧回报 "ovr" 位图）。 */
uint32_t override_mask(void);

/* 通道名 <-> 枚举（协议用字符串）：
 * "lux" "accel" "gyro" "current_a" "enc_rpm" "face"。 */
int         ovr_ch_from_name(const char *name);   /* -1 = 未知 */
const char *ovr_ch_name(ovr_ch_t ch);

#ifdef __cplusplus
}
#endif
