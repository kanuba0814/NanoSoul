#pragma once
/*
 * app_sense — FACE-mode sensor feed. The legacy TESTPANEL wired the encoders /
 * INA219; the FACE runtime never did, so telemetry showed no wheel RPM or motor
 * current. This starts them (harmless when wheels are unwired: PCNT idles, the
 * current sensor probes absent) and publishes encoder + current telemetry at
 * 10 Hz, plus WHEEL_MOVED when a wheel turns while we are not driving (pushed
 * by hand — docs/12 S9), plus the stall protector (docs/12 S14).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t app_sense_start(void);

/* ---- stall protection FSM (pure; for stall_sim). docs/12 S14 ---- */

typedef enum {
    STALL_OK = 0,   /* nothing to do */
    STALL_TRIP,     /* just tripped: cut motors + fault */
    STALL_RETRY,    /* retry window: re-enable and see if it clears */
    STALL_GIVEUP,   /* retries exhausted: stay locked until clear_fault */
} stall_action_t;

typedef struct {
    bool    tripped;
    int64_t cond_since_ms;
    int     tries;
    int64_t next_try_ms;
} stall_fsm_t;

/* cond = stall condition true this sample (high current + driven + not turning). */
stall_action_t stall_step(stall_fsm_t *s, bool cond, int stall_ms,
                          int retry_gap_ms, int max_retry, int64_t now_ms);
void stall_reset(stall_fsm_t *s);
