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

/* Pure decision core + its carried state. Also used by the soul_sim selftest. */
typedef struct {
    soul_state_t state;
    float        vx, vy, wz;   /* motion intent */
    const char  *emotion;
    float        gaze_ms;      /* accumulated frontal gaze in ENGAGE */
    int64_t      gazed_until_ms;
    bool         fault;
    soul_state_t session;      /* SOUL_IDLE = none, else LISTEN/THINK/SPEAK */
} soul_ctx_t;

void soul_eval(soul_ctx_t *c, const tel_face_t *face,
               const ns_behavior_cfg_t *b, int dt_ms, int64_t now_ms);

#ifdef __cplusplus
}
#endif
