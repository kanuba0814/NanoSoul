#pragma once
/*
 * soul — the local perception→decision state machine. NanoSoul's differentiating
 * core: it fuses the face observation (distance proxy + centering + frontal
 * gaze) with external stimuli (voice session, faults) and decides what to do —
 * approach / retreat / center / back off when stared at — driving the motion
 * intent and the face emotion. Fully local; the cloud LLM never enters this loop.
 *
 * The decision logic lives in a pure function `soul_eval()` (no hardware), so it
 * runs both in the live 10 Hz task and in the soul_sim selftest with injected
 * observations.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ns_config.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t    soul_init(void);
esp_err_t    soul_start(void);
soul_state_t soul_current(void);

/* External stimuli (from voice / companion link). */
void soul_notify_wake(void);
void soul_set_session(soul_state_t s);      /* LISTEN/THINK/SPEAK, SOUL_IDLE to clear */
void soul_notify_fault(const char *reason);
void soul_clear_fault(void);
void soul_emotion_override(const char *name, uint32_t ms);
void soul_set_pc(const tel_pc_t *pc);       /* PC-state fusion input (docs/12 §3.5) */

/* Interaction permission level derived from PC state (docs/12 §3.5): a modulator
 * on behavior, never a direct trigger. */
typedef enum {
    PERM_NORMAL = 0,   /* no/stale PC info, or ordinary — full behavior */
    PERM_QUIET,        /* focused work / dnd — no proactive approach or invite */
    PERM_SILENT,       /* meeting — hold still */
    PERM_INVITE,       /* user idle at desk — invitation to play allowed */
    PERM_AWAY_WAIT,    /* PC active but no face — don't sulk, glance at screen */
    PERM_REST,         /* locked / long idle — hasten DOZE */
} soul_perm_t;

/* All per-tick inputs to the decision core. */
typedef struct {
    tel_face_t  face;
    bool        lifted;      /* IMU: currently held aloft */
    bool        dark;        /* ambient below dark threshold (hysteresis) */
    int         tap_count;   /* taps consumed this tick (0/1/2) */
    bool        touched;
    bool        touch_long;
    bool        wheel_moved;
    bool        loud;
    soul_perm_t perm;
} soul_inputs_t;

/* Pure PC-state fusion (docs/12 §3.5). */
soul_perm_t soul_perm_eval(const tel_pc_t *pc, bool face_present,
                           const ns_pc_cfg_t *cfg, int64_t now_ms);

/* Pure decision core + its carried state. Also used by the soul_sim selftest. */
typedef struct {
    soul_state_t state;
    float        vx, vy, wz;   /* motion intent */
    const char  *emotion;
    float        gaze_ms;      /* accumulated frontal gaze in ENGAGE */
    int64_t      gazed_until_ms;
    int64_t      gaze_cooldown_until_ms; /* GAZED re-trigger cooldown */
    bool         fault;
    soul_state_t session;      /* SOUL_IDLE = none, else LISTEN/THINK/SPEAK */
} soul_ctx_t;

void soul_eval(soul_ctx_t *c, const soul_inputs_t *in,
               const ns_behavior_cfg_t *b, int dt_ms, int64_t now_ms);

#ifdef __cplusplus
}
#endif
