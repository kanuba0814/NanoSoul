#pragma once
/*
 * imu — QMI8658 6-axis IMU on board I2C1 (docs/12 S7/S8). Polls the accelerometer
 * at 100 Hz and derives tap / lifted / placed / tilt in SOFTWARE (the chip's
 * on-chip event engine is deferred to P1 — CTRL9 host-command protocol is fussy).
 * The detectors are pure functions so imu_sim can validate them with synthetic
 * sequences. Register map + scaling per the SensorLib / Waveshare QMI8658 driver
 * (WHO_AM_I=0x05, accel regs 0x35..0x3A little-endian, ±4g = 8192 LSB/g).
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- live API --- */
esp_err_t imu_init(i2c_master_bus_handle_t bus);  /* probe+config+task; absent -> ESP_OK */
bool      imu_present(void);
bool      imu_lifted(void);    /* current lifted level */
bool      imu_tilted(void);    /* pitch/roll beyond threshold (for protect) */
void      imu_last_accel(float out[3]); /* 最近一帧 m/s^2（含测试覆盖）供 sense 流 */

/* --- pure detectors (for imu_sim; no hardware). Accel in m/s^2. --- */

typedef struct {
    float   prev[3];
    bool    have_prev;
    int64_t last_impulse_ms;
    int64_t pending_since_ms;
    bool    pending;
} imu_tap_state_t;
/* Feed one sample. Returns 0 none / 1 single / 2 double. th = jerk m/s^2. */
int imu_tap_feed(imu_tap_state_t *s, const float a[3], float th, int64_t now_ms);

typedef struct {
    bool    lifted;
    int64_t dev_since_ms;
    int64_t still_since_ms;
} imu_lift_state_t;
/* Feed one sample. Returns +1 LIFTED edge / -1 PLACED edge / 0 none. */
int imu_lift_feed(imu_lift_state_t *s, const float a[3], float dev_frac,
                  int hold_ms, int place_ms, int64_t now_ms);

/* True if |pitch| or |roll| (from accel) exceeds `deg`. */
bool imu_tilt_eval(const float a[3], int deg);

#ifdef __cplusplus
}
#endif
