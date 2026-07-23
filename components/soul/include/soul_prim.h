#pragma once
/*
 * soul_prim — the motion primitive player (docs/12 §5). Each primitive is a
 * time-parameterized body-frame velocity curve (vx,vy,wz), with a 100 ms
 * ramp in/out and a caller-supplied amplitude scale (the live layer folds a
 * ±15% jitter into that scale; tests pass 1.0 for determinism). Pure logic so
 * prim_sim can validate the curves. Primitives override the state machine's raw
 * intent while active (soul routes them through MOTION_SRC_BEHAVIOR).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRIM_NONE = 0,
    PRIM_NUDGE,        /* short backward step (tap recoil) */
    PRIM_RETREAT_SHY,  /* back off + turn away (GAZED) */
    PRIM_WIGGLE,       /* wz sine wobble (speaking rhythm) */
    PRIM_TILT_GLANCE,  /* glance to one side and back */
    PRIM_SCAN,         /* sweep left/right (look for a lost face) */
    PRIM_SPIN,         /* slow full rotation (autonomy) */
    PRIM_MAX,
} soul_prim_id_t;

typedef struct {
    soul_prim_id_t id;
    int64_t        start_ms;
    int64_t        dur_ms;
    float          scale;      /* amplitude multiplier (1.0 nominal) */
} soul_prim_t;

void soul_prim_start(soul_prim_t *p, soul_prim_id_t id, float scale, int64_t now_ms);

/* Advance to now_ms. Fills out[3]={vx,vy,wz}; returns true while active,
 * false (and clears to PRIM_NONE) once the primitive has finished. */
bool soul_prim_tick(soul_prim_t *p, int64_t now_ms, float out[3]);

bool        soul_prim_active(const soul_prim_t *p);
const char *soul_prim_name(soul_prim_id_t id);

#ifdef __cplusplus
}
#endif
