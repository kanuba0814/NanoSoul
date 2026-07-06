#include "soul_prim.h"

#include <math.h>

#define RAMP_MS 100

static const struct {
    int64_t     dur_ms;
    const char *name;
} PRIM_DEF[PRIM_MAX] = {
    [PRIM_NONE]        = {    0, "none" },
    [PRIM_NUDGE]       = {  600, "nudge" },
    [PRIM_RETREAT_SHY] = {  800, "retreat_shy" },
    [PRIM_WIGGLE]      = { 1200, "wiggle" },
    [PRIM_TILT_GLANCE] = {  800, "tilt_glance" },
    [PRIM_SCAN]        = { 3000, "scan" },
    [PRIM_SPIN]        = { 4000, "spin" },
};

/* 0..1 trapezoidal envelope: ramp up, hold, ramp down. */
static float envelope(int64_t t, int64_t dur)
{
    if (t <= 0 || t >= dur) {
        return 0.0f;
    }
    if (t < RAMP_MS) {
        return (float)t / RAMP_MS;
    }
    if (t > dur - RAMP_MS) {
        return (float)(dur - t) / RAMP_MS;
    }
    return 1.0f;
}

void soul_prim_start(soul_prim_t *p, soul_prim_id_t id, float scale, int64_t now_ms)
{
    if (id <= PRIM_NONE || id >= PRIM_MAX) {
        p->id = PRIM_NONE;
        return;
    }
    p->id = id;
    p->start_ms = now_ms;
    p->dur_ms = PRIM_DEF[id].dur_ms;
    p->scale = scale;
}

bool soul_prim_tick(soul_prim_t *p, int64_t now_ms, float out[3])
{
    out[0] = out[1] = out[2] = 0.0f;
    if (p->id <= PRIM_NONE || p->id >= PRIM_MAX) {
        return false;
    }
    int64_t t = now_ms - p->start_ms;
    if (t >= p->dur_ms) {
        p->id = PRIM_NONE;
        return false;
    }
    float env = envelope(t, p->dur_ms) * p->scale;
    float ph  = (float)t / (float)p->dur_ms;   /* 0..1 progress */

    switch (p->id) {
    case PRIM_NUDGE:
        out[0] = -0.4f * env;                          /* back step */
        break;
    case PRIM_RETREAT_SHY:
        out[0] = -0.4f * env;                          /* back off + turn away */
        out[2] =  0.4f * env;
        break;
    case PRIM_WIGGLE:
        out[2] = 0.15f * p->scale * sinf(2.0f * (float)M_PI * t / 600.0f);
        break;
    case PRIM_TILT_GLANCE:
        out[2] = 0.2f * p->scale * sinf((float)M_PI * ph);  /* out and back */
        break;
    case PRIM_SCAN:
        out[2] = 0.2f * p->scale * sinf(2.0f * (float)M_PI * t / 1500.0f);
        break;
    case PRIM_SPIN:
        out[2] = 0.25f * env;                          /* slow steady turn */
        break;
    default:
        break;
    }
    return true;
}

bool soul_prim_active(const soul_prim_t *p)
{
    return p->id > PRIM_NONE && p->id < PRIM_MAX;
}

const char *soul_prim_name(soul_prim_id_t id)
{
    if (id <= PRIM_NONE || id >= PRIM_MAX) {
        return "none";
    }
    return PRIM_DEF[id].name;
}
