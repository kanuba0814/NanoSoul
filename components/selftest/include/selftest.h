#pragma once
/*
 * selftest — auto-loop diagnostics harness.
 *
 * Components register check functions; SELFTEST boot mode runs every check on a
 * loop and emits one machine-readable JSON line per item to the serial log, so
 * a single flash yields the full result matrix and the developer can iterate to
 * all-green without a human driving each step. Pure-logic checks (motion IK,
 * soul FSM) run fully unattended; hardware checks SKIP gracefully when the part
 * is absent (SKIP is not a failure). Real-human checks are marked MANUAL.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_PASS = 0,
    ST_FAIL,
    ST_SKIP,
} st_result_t;

typedef struct {
    st_result_t result;
    char        detail[64];
} st_report_t;

typedef st_report_t (*st_fn_t)(void);

#define SELFTEST_MAX   48
#define SELFTEST_FLAG_MANUAL  (1u << 0)  /* needs a human to fully confirm */

esp_err_t   selftest_init(void);
esp_err_t   selftest_register(const char *name, st_fn_t fn, uint32_t flags);
int         selftest_count(void);
const char *selftest_item_name(int idx);
st_report_t selftest_last(int idx);
uint32_t    selftest_flags(int idx);

/* Run one full round: log JSON per item + post NS_EVT_SELFTEST_ITEM. Returns
 * the number of hard failures (SKIP/MANUAL not counted). */
int  selftest_run_round(int round);

/* Blocking: run rounds forever with `interval_ms` between them. */
void selftest_run_loop(uint32_t interval_ms);

/* Helpers for building reports inside a check fn (printf-style detail). */
st_report_t st_pass(const char *fmt, ...);
st_report_t st_fail(const char *fmt, ...);
st_report_t st_skip(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
