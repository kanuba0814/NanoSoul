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

/* Intent sources, highest priority first. The lowest-numbered source with an
 * unexpired lease wins and drives; every source auto-releases when its lease
 * expires (no explicit release). See docs/12 §2. */
typedef enum {
    MOTION_SRC_REFLEX = 0,   /* lifted / tilt / stall — zero intent, top priority */
    MOTION_SRC_FAULT,        /* soul FAULT hold (renewed each tick while faulted) */
    MOTION_SRC_TELEOP,       /* companion teleop, lease = ttl_ms */
    MOTION_SRC_BEHAVIOR,     /* soul behavior layer (incl. session hold-still)     */
    MOTION_SRC_MICRO,        /* idle micro-motion, only when everything else idle  */
    MOTION_SRC_MAX,
} motion_src_t;

typedef struct {
    float   vx, vy, wz;
    int64_t deadline_ms;     /* absolute ms; <= now = inactive */
} motion_slot_t;

esp_err_t motion_init(int max_duty_pct, bool enabled);
void      motion_set_apply(motion_apply_fn fn);
void      motion_set_enabled(bool on);
bool      motion_enabled(void);

// Request motion from a prioritized source with a lease (ttl_ms). Re-arbitrates
// immediately; a 100 ms watchdog re-arbitrates on lease expiry.
void      motion_request(motion_src_t src, float vx, float vy, float wz, uint32_t ttl_ms);

// Body-frame intent. Convenience alias for the BEHAVIOR source (250 ms lease).
void      motion_set_intent(float vx, float vy, float wz);
void      motion_stop(void);   // hard clear: drop all leases, drive zero.

// Pure arbiter (for tests): winning source, or MOTION_SRC_MAX if none active.
motion_src_t motion_arbitrate(const motion_slot_t slots[MOTION_SRC_MAX], int64_t now_ms);

// Pure IK (for tests + reuse): signed duty in [-MOTION_DUTY_MAX, +MOTION_DUTY_MAX].
void      motion_ik(float vx, float vy, float wz, int max_duty_pct, int16_t duty[3]);

#ifdef __cplusplus
}
#endif
