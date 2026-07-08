#include "soul.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "face.h"
#include "motion.h"
#include "soul_expr.h"
#include "soul_prim.h"
#include "vision.h"

/* ±15% amplitude jitter for a primitive start (docs/12 P3). */
static float prim_jitter(void)
{
    return 1.0f + ((float)(esp_random() % 31) - 15.0f) / 100.0f;
}

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
            wz = center_wz;
            /* turn to face first (|cx|>0.15 = only turn); then advance at a speed
             * proportional to how far the face still is (docs/12 S4). */
            if (fabsf(face->cx) < 0.15f) {
                float err = (b->near_lo - area) / b->near_lo;   /* 0..1 */
                if (err > 1.0f) err = 1.0f;
                vx = SOUL_SLOW * (0.4f + 0.6f * err);
            }
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

static int soul_mem_count_recent(uint8_t kind, int64_t window_ms, int64_t now_ms)
{
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (s_mem[i].ts && s_mem[i].kind == kind && (now_ms - s_mem[i].ts) <= window_ms) {
            n++;
        }
    }
    return n;
}

/* S5 shy tip variants, picked at random when GAZED fires. */
static const char *SHY_TIPS[] = {
    "别一直盯着看啦", "我会害羞的", "看什么看~", "唔…被发现了",
};

/* 认主 greeting variants, picked at random when OWNER_SEEN fires. */
static const char *OWNER_TIPS[] = {
    "主人！", "主人你回来啦", "是你呀~", "嘿嘿 主人",
};

/* ---- event latch: the event-loop task latches interaction events; the soul
 * task drains them into soul_inputs_t each tick. Simple flags/counters, so a
 * benign one-tick race is fine (no lock). ---- */
static volatile struct {
    int  tap_pending;
    bool double_pending;
    bool lifted;         /* level: LIFTED sets, PLACED clears */
    bool dark;           /* level: DARK sets, BRIGHT clears   */
    bool touch_pending;
    bool touch_long_pending;
    bool wheel_pending;
    bool loud_pending;
    bool owner_pending;      /* 认主: enrolled face recognized this episode */
    bool stranger_pending;   /* 认主: face concluded unenrolled */
    bool enrolled_pending;   /* 认主: enrollment just completed */
} s_latch;

static tel_pc_t   s_pc;               /* last PC state from companion */
static float      s_energy = 0.5f;
static float      s_social = 0.5f;
static soul_prim_t s_prim;            /* active motion primitive (docs/12 §5) */
static int64_t    s_idle_since;       /* when the current IDLE stretch began */
static int64_t    s_autonomy_next;    /* earliest next self-initiated action */

static void soul_evt_handler(void *a, esp_event_base_t base, int32_t id, void *data)
{
    (void)a; (void)base; (void)data;
    switch ((ns_event_id_t)id) {
    case NS_EVT_TAP: {
        ns_evt_tap_t *e = data;
        if (e && e->count >= 2) {
            s_latch.double_pending = true;
        } else {
            s_latch.tap_pending++;
        }
        break;
    }
    case NS_EVT_LIFTED:      s_latch.lifted = true;      break;
    case NS_EVT_PLACED:      s_latch.lifted = false;     break;
    case NS_EVT_DARK:        s_latch.dark = true;        break;
    case NS_EVT_BRIGHT:      s_latch.dark = false;       break;
    case NS_EVT_TOUCH: {
        ns_evt_touch_t *e = data;
        if (e && e->long_press) {
            s_latch.touch_long_pending = true;
        } else {
            s_latch.touch_pending = true;
        }
        break;
    }
    case NS_EVT_WHEEL_MOVED: s_latch.wheel_pending = true; break;
    case NS_EVT_LOUD:        s_latch.loud_pending = true;  break;
    case NS_EVT_OWNER_SEEN:    s_latch.owner_pending = true;    break;
    case NS_EVT_STRANGER_SEEN: s_latch.stranger_pending = true; break;
    case NS_EVT_FACE_ENROLLED: s_latch.enrolled_pending = true; break;
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
        in.tap_count    = taps;
        in.double_tap   = s_latch.double_pending;  s_latch.double_pending = false;
        in.lifted       = s_latch.lifted;
        in.dark         = s_latch.dark;
        in.touched      = s_latch.touch_pending;      s_latch.touch_pending = false;
        in.touch_long   = s_latch.touch_long_pending; s_latch.touch_long_pending = false;
        in.wheel_moved  = s_latch.wheel_pending;      s_latch.wheel_pending = false;
        in.loud         = s_latch.loud_pending;       s_latch.loud_pending = false;
        in.perm         = soul_perm_eval(&s_pc, in.face.present, &cfg->pc, now_ms);

        soul_eval(&s_ctx, &in, &cfg->behavior, SOUL_TICK_MS, now_ms);

        /* S7 tap reactions (a transient, not a state). */
        if (in.tap_count > 0) {
            int recent = soul_mem_count_recent(MEM_TAP, 60000, now_ms);
            soul_mem_note(MEM_TAP, now_ms);
            soul_expr_transient(&s_expr, "o", "tap", 800, now_ms);
            if (recent >= 1) {                     /* 2nd+ tap within 60 s -> escalate */
                face_set_tip("别敲啦");
                s_social -= 0.1f;
                if (s_social < 0) s_social = 0;
                soul_prim_start(&s_prim, PRIM_NUDGE, 2.0f * prim_jitter(), now_ms);
            } else {
                face_set_tip("呀");
                soul_prim_start(&s_prim, PRIM_NUDGE, prim_jitter(), now_ms);
            }
        }
        if (in.double_tap) {                       /* S7 easter egg: cycle the face */
            static const char *CLIPS[] = { "waiting", "o", "sad", "sleep", "think" };
            static int ci = 0;
            ci = (ci + 1) % (int)(sizeof(CLIPS) / sizeof(CLIPS[0]));
            soul_expr_transient(&s_expr, CLIPS[ci], "egg", 1500, now_ms);
            face_set_tip("换个脸~");
        }
        if (in.touched) {                          /* poke the screen -> pleased */
            soul_expr_transient(&s_expr, "o", "touch", 800, now_ms);
            face_set_tip("嘿嘿");
            s_social += 0.05f;
            if (s_social > 1.0f) s_social = 1.0f;
        }
        if (in.touch_long && s_ctx.fault) {        /* long press clears a fault (S14) */
            soul_clear_fault();
            face_set_tip("好啦好啦");
        }
        if (in.loud) {                             /* startled by a sudden noise */
            soul_expr_transient(&s_expr, "o", "loud", 800, now_ms);
        }

        /* 认主 reactions (events posted by vision, once per presence episode).
         * Expression + tip only — no motion; the state machine keeps driving. */
        bool owner_seen = s_latch.owner_pending;       s_latch.owner_pending = false;
        bool stranger_seen = s_latch.stranger_pending; s_latch.stranger_pending = false;
        bool enrolled = s_latch.enrolled_pending;      s_latch.enrolled_pending = false;
        if (enrolled) {
            soul_expr_transient(&s_expr, "o", "enroll", 1500, now_ms);
            face_set_tip("记住你啦！");
            s_social += 0.1f;
            if (s_social > 1.0f) s_social = 1.0f;
        } else if (owner_seen) {
            soul_expr_transient(&s_expr, "o", "owner", 1500, now_ms);
            face_set_tip(OWNER_TIPS[esp_random() % (sizeof(OWNER_TIPS) / sizeof(OWNER_TIPS[0]))]);
            s_social += 0.1f;
            if (s_social > 1.0f) s_social = 1.0f;
        } else if (stranger_seen) {
            soul_expr_transient(&s_expr, "think", "stranger", 1500, now_ms);
            face_set_tip("你是谁呀？");
        }
        if (in.wheel_moved) {                      /* S9: pushed by hand -> curious/play */
            static int     wheel_run;
            static int64_t wheel_last;
            soul_expr_transient(&s_expr, "o", "wheel", 800, now_ms);
            wheel_run = (now_ms - wheel_last < 5000) ? wheel_run + 1 : 1;
            wheel_last = now_ms;
            if (wheel_run >= 3) {
                face_set_tip("好玩吗 :P");
                s_social += 0.05f;
                if (s_social > 1.0f) s_social = 1.0f;
            } else {
                face_set_tip("咦？");
            }
        }

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

        if (++perf_div >= 10) {                            /* ~1 Hz mood publish */
            perf_div = 0;
            telemetry_set_mood(s_energy, s_social);
        }

        /* S11 autonomy: after a long idle, self-initiate a ZERO-TRANSLATION action
         * (rotation / sway / a mutter) — never a translation, since there are no
         * cliff sensors to move blindly (docs/12 S11). */
        if (cfg->behavior.autonomy && s_ctx.state == SOUL_IDLE && !soul_prim_active(&s_prim) &&
            in.perm != PERM_SILENT && in.perm != PERM_QUIET) {
            if (s_idle_since == 0) {
                s_idle_since = now_ms;
            }
            if ((now_ms - s_idle_since) > (int64_t)cfg->behavior.autonomy_idle_s * 1000 &&
                now_ms >= s_autonomy_next) {
                switch (esp_random() % 3) {
                case 0: soul_prim_start(&s_prim, PRIM_SCAN, prim_jitter(), now_ms); break;
                case 1: soul_prim_start(&s_prim, PRIM_SPIN, prim_jitter(), now_ms); break;
                default:
                    soul_expr_transient(&s_expr, "sleep", "idle", 3000, now_ms);
                    face_set_tip("今天真安静");
                    break;
                }
                int gap = 45000 + (int)((1.0f - s_energy) * 255000);   /* 45s..300s by energy */
                s_autonomy_next = now_ms + gap;
            }
        } else if (s_ctx.state != SOUL_IDLE) {
            s_idle_since = 0;
        }

        /* Motion: an active primitive overrides the state machine's raw intent;
         * FAULT holds the top-priority zero; a still state gets gentle micro-motion;
         * otherwise the state intent drives. */
        float pv[3];
        if (s_ctx.state == SOUL_FAULT) {
            motion_request(MOTION_SRC_FAULT, 0.0f, 0.0f, 0.0f, 250);
            telemetry_set_beh("FAULT");
        } else if (soul_prim_tick(&s_prim, now_ms, pv)) {
            motion_request(MOTION_SRC_BEHAVIOR, pv[0], pv[1], pv[2], 250);
            telemetry_set_beh(soul_prim_name(s_prim.id));
        } else {
            bool still = fabsf(s_ctx.vx) < 0.01f && fabsf(s_ctx.vy) < 0.01f && fabsf(s_ctx.wz) < 0.01f;
            if (still && in.perm != PERM_SILENT &&
                (s_ctx.state == SOUL_IDLE || s_ctx.state == SOUL_ENGAGE)) {
                /* S3 micro-motion: gentle breathing sway, lowest priority */
                float mw = 0.04f * sinf(2.0f * (float)M_PI * (float)now_ms / 4000.0f);
                motion_request(MOTION_SRC_MICRO, 0.0f, 0.0f, mw, 250);
                telemetry_set_beh("micro");
            } else {
                motion_set_intent(s_ctx.vx, s_ctx.vy, s_ctx.wz);
                telemetry_set_beh(soul_state_name(s_ctx.state));
            }
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
                soul_prim_start(&s_prim, PRIM_RETREAT_SHY, prim_jitter(), now_ms);  /* S5 */
            } else if (s_ctx.state == SOUL_LIFTED) {           /* S8 picked up */
                soul_expr_transient(&s_expr, "o", "lift", 1200, now_ms);
                face_set_tip("哇——放我下来");
            } else if (last_state == SOUL_LIFTED) {            /* S8 set back down */
                soul_mem_note(MEM_LIFTED, now_ms);
                face_set_tip("唔…吓死我了");
            } else if (last_state == SOUL_DOZE) {              /* S10 waking: rub eyes */
                soul_expr_transient(&s_expr, "o", "wake", 1000, now_ms);
            } else if (s_ctx.state == SOUL_SPEAK) {            /* S12 speaking rhythm */
                soul_prim_start(&s_prim, PRIM_WIGGLE, prim_jitter(), now_ms);
            } else if (s_ctx.state == SOUL_IDLE &&
                       (last_state == SOUL_ENGAGE || last_state == SOUL_APPROACH ||
                        last_state == SOUL_RETREAT || last_state == SOUL_GAZED)) {
                soul_prim_start(&s_prim, PRIM_SCAN, prim_jitter(), now_ms);  /* S6 look for lost face */
                face_set_tip("咦？人呢");
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
