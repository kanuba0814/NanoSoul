#pragma once
/*
 * motion — three-wheel omni inverse kinematics + output gate.
 *
 * Takes a body-frame velocity intent (vx forward, vy left, wz CCW, each ~[-1,1])
 * and computes a signed duty per wheel. When motion is disabled (the default —
 * wheels are not wired in this build) it ONLY computes and publishes the duty to
 * telemetry/HUD so the numbers are visible; it never drives the motors. When
 * enabled, it hands the duty to a registered apply callback (which owns the H-
 * bridge driver, so motion stays free of the drv_motor dependency).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Must match drv_motor.h MOTOR_DUTY_MAX (LEDC 10-bit). */
#define MOTION_DUTY_MAX 1023

typedef void (*motion_apply_fn)(const int16_t duty[3]);

esp_err_t motion_init(int max_duty_pct, bool enabled);
void      motion_set_apply(motion_apply_fn fn);
void      motion_set_enabled(bool on);
bool      motion_enabled(void);

// Body-frame intent. Recomputes duty, publishes telemetry, drives if enabled.
void      motion_set_intent(float vx, float vy, float wz);
void      motion_stop(void);

// Pure IK (for tests + reuse): signed duty in [-MOTION_DUTY_MAX, +MOTION_DUTY_MAX].
void      motion_ik(float vx, float vy, float wz, int max_duty_pct, int16_t duty[3]);

#ifdef __cplusplus
}
#endif
