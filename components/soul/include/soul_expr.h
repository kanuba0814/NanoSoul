#pragma once
/*
 * soul_expr — expression selection engine (docs/12 §3.3, rules E1–E5).
 *
 * Pure logic (no hardware, no globals) so the soul task and the expr_sim
 * selftest share it. It decides which emotion to display this tick given the
 * state-machine's baseline emotion, plus any transient event emotion, override
 * lease, or fault. Priority: FAULT > transient (NOW, held) > override (10 s) >
 * baseline (FADE, throttled). Debounce is the whole point: transient events get
 * a minimum hold and a same-kind cooldown; baseline switches are rate-limited.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXPR_TRANSIENT_HOLD_MS  800   /* default min hold for a transient */
#define EXPR_KIND_COOLDOWN_MS  1500   /* same-kind transient re-trigger cooldown */
#define EXPR_BASELINE_MIN_MS   2000   /* min interval between baseline switches */

typedef struct {
    char    cur[16];                  /* emotion currently shown */
    char    transient_emo[16];        /* transient event emotion */
    char    transient_kind[12];       /* kind label (for cooldown) */
    int64_t transient_until_ms;       /* transient hold expiry */
    int64_t kind_cooldown_until_ms;   /* same-kind cooldown expiry (single slot) */
    int64_t baseline_last_switch_ms;  /* last baseline switch, for min interval */
    char    override_emo[16];         /* companion set_emotion override */
    int64_t override_until_ms;        /* override lease expiry */
} soul_expr_state_t;

void soul_expr_init(soul_expr_state_t *s);

/* Register a transient event emotion (E2). Ignored if the same kind is still
 * cooling down. hold_ms 0 -> EXPR_TRANSIENT_HOLD_MS. */
void soul_expr_transient(soul_expr_state_t *s, const char *emo, const char *kind,
                         uint32_t hold_ms, int64_t now_ms);

/* Register / clear an override (E3). name NULL or "" clears it. */
void soul_expr_override(soul_expr_state_t *s, const char *name, uint32_t ms, int64_t now_ms);

/* Decide this tick's emotion. `baseline` = state-machine's desired emotion;
 * `fault` = FAULT active. Returns true and fills *out_emo (points into `s`,
 * stable until the next call) + *out_now (NOW vs FADE) when a NEW switch should
 * be issued; false = hold current. */
bool soul_expr_decide(soul_expr_state_t *s, const char *baseline, bool fault,
                      int64_t now_ms, const char **out_emo, bool *out_now);

#ifdef __cplusplus
}
#endif
