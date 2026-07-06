#include "soul_expr.h"

#include <string.h>

void soul_expr_init(soul_expr_state_t *s)
{
    memset(s, 0, sizeof(*s));
}

void soul_expr_transient(soul_expr_state_t *s, const char *emo, const char *kind,
                         uint32_t hold_ms, int64_t now_ms)
{
    if (!emo || !emo[0]) {
        return;
    }
    /* Same-kind cooldown: don't re-fire the same reaction back-to-back. */
    if (now_ms < s->kind_cooldown_until_ms && kind && s->transient_kind[0] &&
        strcmp(kind, s->transient_kind) == 0) {
        return;
    }
    strlcpy(s->transient_emo, emo, sizeof(s->transient_emo));
    strlcpy(s->transient_kind, kind ? kind : "", sizeof(s->transient_kind));
    s->transient_until_ms = now_ms + (hold_ms ? hold_ms : EXPR_TRANSIENT_HOLD_MS);
    s->kind_cooldown_until_ms = now_ms + EXPR_KIND_COOLDOWN_MS;
}

void soul_expr_override(soul_expr_state_t *s, const char *name, uint32_t ms, int64_t now_ms)
{
    if (!name || !name[0]) {
        s->override_until_ms = 0;
        s->override_emo[0] = '\0';
        return;
    }
    strlcpy(s->override_emo, name, sizeof(s->override_emo));
    s->override_until_ms = now_ms + ms;
}

bool soul_expr_decide(soul_expr_state_t *s, const char *baseline, bool fault,
                      int64_t now_ms, const char **out_emo, bool *out_now)
{
    const char *want;
    bool now_cut;
    bool is_baseline = false;

    if (fault) {
        want = "sad";
        now_cut = true;
    } else if (now_ms < s->transient_until_ms) {
        want = s->transient_emo;
        now_cut = true;
    } else if (now_ms < s->override_until_ms && s->override_emo[0]) {
        want = s->override_emo;
        now_cut = false;
    } else {
        want = baseline ? baseline : "waiting";
        now_cut = false;
        is_baseline = true;
    }

    if (strcmp(want, s->cur) == 0) {
        return false;   /* already showing it */
    }
    /* Baseline switches are rate-limited to damp state-machine oscillation.
     * Fault / transient / override bypass the throttle. */
    if (is_baseline && (now_ms - s->baseline_last_switch_ms) < EXPR_BASELINE_MIN_MS) {
        return false;
    }

    strlcpy(s->cur, want, sizeof(s->cur));
    if (is_baseline) {
        s->baseline_last_switch_ms = now_ms;
    }
    *out_emo = s->cur;
    *out_now = now_cut;
    return true;
}
