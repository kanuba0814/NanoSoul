#include "soul.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "face.h"
#include "motion.h"
#include "soul_expr.h"
#include "vision.h"

static const char *TAG = "soul";

#define SOUL_TICK_MS   100          /* 10 Hz decision loop */
#define SOUL_SLOW      0.5f         /* nominal approach/retreat speed */
#define SOUL_CENTER_KP 0.8f         /* turn-to-center gain on cx */
#define SOUL_GAZE_BACKOFF_MS 1500   /* how long we shy away after being gazed */

/* ---- pure PC-state fusion (docs/12 §3.5) ---- */
soul_perm_t soul_perm_eval(const tel_pc_t *pc, bool face_present,
                           const ns_pc_cfg_t *cfg, int64_t now_ms)
{
    if (!pc || pc->activity[0] == '\0') {
        return PERM_NORMAL;                              /* never received PC info */
    }
    if ((now_ms - pc->rx_ms) > (int64_t)cfg->stale_s * 1000) {
        return PERM_NORMAL;                              /* stale -> single-source */
    }
    if (cfg->respect_dnd && pc->dnd) {
        return PERM_QUIET;                               /* manual DND wins */
    }

    bool active = strcmp(pc->activity, "active") == 0;
    bool locked = strcmp(pc->activity, "locked") == 0;
    bool idle   = strcmp(pc->activity, "idle") == 0;

    if (face_present) {
        if (cfg->quiet_meeting && strcmp(pc->focus, "meeting") == 0) {
            return PERM_SILENT;
        }
        if (cfg->quiet_work && active && strcmp(pc->focus, "work") == 0) {
            return PERM_QUIET;
        }
        if (idle && pc->idle_s >= cfg->invite_idle_s) {
            return PERM_INVITE;
        }
        return PERM_NORMAL;
    }
    /* nobody in view */
    if (active) {
        return PERM_AWAY_WAIT;                           /* at the desk, out of frame */
    }
    if (locked || idle) {
        return PERM_REST;
    }
    return PERM_NORMAL;
}

/* ---- pure decision core (no hardware) ---- */
void soul_eval(soul_ctx_t *c, const soul_inputs_t *in,
               const ns_behavior_cfg_t *b, int dt_ms, int64_t now_ms)
{
    const tel_face_t *face = &in->face;
    float vx = 0, vy = 0, wz = 0;
    soul_state_t st;

    if (c->fault) {
        st = SOUL_FAULT;
        c->gaze_ms = 0;
    } else if (in->lifted) {
        st = SOUL_LIFTED;           /* held aloft — freeze, look surprised */
        c->gaze_ms = 0;
    } else if (c->session != SOUL_IDLE) {
        st = c->session;            /* LISTEN / THINK / SPEAK — hold still */
        c->gaze_ms = 0;
        if (st == SOUL_LISTEN && face->present) {
            wz = -SOUL_CENTER_KP * face->cx;   /* face the speaker */
        }
    } else if (now_ms < c->gazed_until_ms) {
        st = SOUL_GAZED;            /* shying away after a sustained stare */
        vx = -SOUL_SLOW;
    } else if (!face->present) {
        if (in->dark && in->perm != PERM_AWAY_WAIT) {
            st = SOUL_DOZE;         /* dark + nobody -> sleep */
        } else {
            st = SOUL_IDLE;
            if (b->idle_scan) {
                wz = 0.2f;          /* gentle look-around */
            }
        }
        c->gaze_ms = 0;
    } else if (in->dark) {
        st = SOUL_DOZE;             /* dark holds even with a face until light returns */
        c->gaze_ms = 0;
    } else {
        float area = face->area_ratio;
        float center_wz = -SOUL_CENTER_KP * face->cx;  /* turn toward the face */
        bool proactive_ok = (in->perm != PERM_QUIET && in->perm != PERM_SILENT);
        if (in->perm == PERM_SILENT) {
            st = SOUL_ENGAGE;       /* meeting — sit still and quiet */
            c->gaze_ms = 0;
        } else if (area > b->near_hi) {
            st = SOUL_RETREAT;
            vx = -SOUL_SLOW;
            wz = center_wz;
            c->gaze_ms = 0;
        } else if (area < b->near_lo && proactive_ok) {
            st = SOUL_APPROACH;
            vx = SOUL_SLOW;
            wz = center_wz;
            c->gaze_ms = 0;
        } else {
            st = SOUL_ENGAGE;
            wz = center_wz;
            if (face->frontal_score > b->frontal_thresh &&
                now_ms >= c->gaze_cooldown_until_ms) {
                c->gaze_ms += dt_ms;
                if (c->gaze_ms >= (float)b->gaze_hold_ms) {
                    c->gazed_until_ms = now_ms + SOUL_GAZE_BACKOFF_MS;
                    c->gaze_cooldown_until_ms = now_ms + (int64_t)b->gaze_cooldown_s * 1000;
                    c->gaze_ms = 0;
                    st = SOUL_GAZED;
                    vx = -SOUL_SLOW;
                }
            } else {
                c->gaze_ms = 0;
            }
        }
    }

    c->state = st;
    c->vx = vx;
    c->vy = vy;
    c->wz = wz;

    switch (st) {
    case SOUL_RETREAT:
    case SOUL_GAZED:
    case SOUL_LIFTED:  c->emotion = "o"; break;
    case SOUL_DOZE:    c->emotion = "sleep"; break;
    case SOUL_THINK:   c->emotion = "think"; break;
    case SOUL_SPEAK:
    case SOUL_LISTEN:  c->emotion = "waiting"; break;
    case SOUL_FAULT:   c->emotion = "sad"; break;
    default:           c->emotion = "waiting"; break;  /* IDLE/ENGAGE/APPROACH */
    }
}

/* ---- live wiring ---- */
static soul_ctx_t        s_ctx;
static soul_expr_state_t s_expr;

/* ---- short-term memory ring (docs/12 §3.4) ---- */
typedef enum { MEM_TAP = 0, MEM_GAZED, MEM_LIFTED, MEM_INVITE } soul_mem_kind_t;
static struct { uint8_t kind; int64_t ts; } s_mem[8];
static int s_mem_head;

static void soul_mem_note(uint8_t kind, int64_t now_ms)
{
    s_mem[s_mem_head].kind = kind;
    s_mem[s_mem_head].ts = now_ms;
    s_mem_head = (s_mem_head + 1) % 8;
}

/* S5 shy tip variants, picked at random when GAZED fires. */
static const char *SHY_TIPS[] = {
    "别一直盯着看啦", "我会害羞的", "看什么看~", "唔…被发现了",
};

/* ---- event latch: the event-loop task latches interaction events; the soul
 * task drains them into soul_inputs_t each tick. Simple flags/counters, so a
 * benign one-tick race is fine (no lock). ---- */
static volatile struct {
    int  tap_pending;
    bool lifted;         /* level: LIFTED sets, PLACED clears */
    bool dark;           /* level: DARK sets, BRIGHT clears   */
    bool touch_pending;
    bool wheel_pending;
    bool loud_pending;
} s_latch;

static tel_pc_t s_pc;                 /* last PC state from companion */
static float    s_energy = 0.5f;
static float    s_social = 0.5f;

static void soul_evt_handler(void *a, esp_event_base_t base, int32_t id, void *data)
{
    (void)a; (void)base; (void)data;
    switch ((ns_event_id_t)id) {
    case NS_EVT_TAP:         s_latch.tap_pending++;      break;
    case NS_EVT_LIFTED:      s_latch.lifted = true;      break;
    case NS_EVT_PLACED:      s_latch.lifted = false;     break;
    case NS_EVT_DARK:        s_latch.dark = true;        break;
    case NS_EVT_BRIGHT:      s_latch.dark = false;       break;
    case NS_EVT_TOUCH:       s_latch.touch_pending = true; break;
    case NS_EVT_WHEEL_MOVED: s_latch.wheel_pending = true; break;
    case NS_EVT_LOUD:        s_latch.loud_pending = true;  break;
    default: break;
    }
}

void soul_set_pc(const tel_pc_t *pc)
{
    if (pc) {
        s_pc = *pc;
    }
}

static void soul_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    soul_state_t last_state = SOUL_STATE_MAX;
    int perf_div = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SOUL_TICK_MS));
        int64_t now_ms = esp_timer_get_time() / 1000;

        soul_inputs_t in = {0};
        if (vision_ready()) {
            vision_get(&in.face);
        }
        int taps = s_latch.tap_pending;
        s_latch.tap_pending = 0;
        in.tap_count    = taps > 2 ? 2 : taps;
        in.lifted       = s_latch.lifted;
        in.dark         = s_latch.dark;
        in.touched      = s_latch.touch_pending;   s_latch.touch_pending = false;
        in.wheel_moved  = s_latch.wheel_pending;   s_latch.wheel_pending = false;
        in.loud         = s_latch.loud_pending;    s_latch.loud_pending = false;
        in.perm         = soul_perm_eval(&s_pc, in.face.present, &cfg->pc, now_ms);

        soul_eval(&s_ctx, &in, &cfg->behavior, SOUL_TICK_MS, now_ms);

        /* mood slow variables (docs/12 §3.1): idle energy recovery + social decay */
        if (cfg->mood.enabled) {
            s_energy += 0.1f * SOUL_TICK_MS / 600000.0f;   /* +0.1 per 10 min */
            if (s_energy > 1.0f) s_energy = 1.0f;
            float tau_ms = (float)cfg->mood.social_tau_min * 60000.0f;
            if (tau_ms > 0) {
                s_social -= s_social * SOUL_TICK_MS / tau_ms;
                if (s_social < 0) s_social = 0;
            }
        }

        telemetry_set_beh(soul_state_name(s_ctx.state));
        if (++perf_div >= 10) {                            /* ~1 Hz mood publish */
            perf_div = 0;
            telemetry_set_mood(s_energy, s_social);
        }

        if (s_ctx.state == SOUL_FAULT) {
            /* Hold on the FAULT source so teleop can't move a faulted robot. */
            motion_request(MOTION_SRC_FAULT, 0.0f, 0.0f, 0.0f, 250);
        } else {
            motion_set_intent(s_ctx.vx, s_ctx.vy, s_ctx.wz);
        }
        telemetry_set_soul(s_ctx.state);

        if (s_ctx.state != last_state) {
            ns_evt_soul_t e = { .from = last_state, .to = s_ctx.state };
            telemetry_post(NS_EVT_SOUL_TRANSITION, &e, sizeof(e));
            if (s_ctx.state == SOUL_GAZED) {
                telemetry_post(NS_EVT_GAZED, NULL, 0);
                soul_mem_note(MEM_GAZED, now_ms);
                soul_expr_transient(&s_expr, "o", "gazed", 800, now_ms);
                face_set_tip(SHY_TIPS[esp_random() % (sizeof(SHY_TIPS) / sizeof(SHY_TIPS[0]))]);
            }
            last_state = s_ctx.state;
        }

        const char *emo;
        bool        now_cut;
        if (soul_expr_decide(&s_expr, s_ctx.emotion, s_ctx.state == SOUL_FAULT,
                             now_ms, &emo, &now_cut)) {
            face_set_emotion(emo, now_cut ? FACE_NOW : FACE_FADE);
            telemetry_set_emotion(emo);
        }
    }
}

esp_err_t soul_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = SOUL_IDLE;
    s_ctx.session = SOUL_IDLE;
    s_ctx.emotion = "waiting";
    soul_expr_init(&s_expr);

    const ns_config_t *cfg = ns_config_get();
    s_energy = cfg->mood.energy_init;
    s_social = cfg->mood.social_init;
    return ESP_OK;
}

esp_err_t soul_start(void)
{
    esp_event_handler_instance_register(NANOSOUL_EVENT, ESP_EVENT_ANY_ID,
                                        soul_evt_handler, NULL, NULL);
    return xTaskCreatePinnedToCore(soul_task, "soul", 4096, NULL, 5, NULL, 0) == pdPASS
               ? ESP_OK : ESP_FAIL;
}

soul_state_t soul_current(void) { return s_ctx.state; }

void soul_notify_wake(void)
{
    telemetry_post(NS_EVT_WAKE, NULL, 0);
    soul_set_session(SOUL_LISTEN);
}

void soul_set_session(soul_state_t s)
{
    s_ctx.session = s;
}

void soul_notify_fault(const char *reason)
{
    s_ctx.fault = true;
    ESP_LOGW(TAG, "FAULT: %s", reason ? reason : "?");
    ns_evt_text_t e = {0};
    strlcpy(e.text, reason ? reason : "fault", sizeof(e.text));
    telemetry_post(NS_EVT_FAULT, &e, sizeof(e));
}

void soul_clear_fault(void)
{
    s_ctx.fault = false;
}

void soul_emotion_override(const char *name, uint32_t ms)
{
    soul_expr_override(&s_expr, name, ms, esp_timer_get_time() / 1000);
}
