#include "soul.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "face.h"
#include "motion.h"
#include "vision.h"

static const char *TAG = "soul";

#define SOUL_TICK_MS   100          /* 10 Hz decision loop */
#define SOUL_SLOW      0.5f         /* nominal approach/retreat speed */
#define SOUL_CENTER_KP 0.8f         /* turn-to-center gain on cx */
#define SOUL_GAZE_BACKOFF_MS 1500   /* how long we shy away after being gazed */

/* ---- pure decision core (no hardware) ---- */
void soul_eval(soul_ctx_t *c, const tel_face_t *face,
               const ns_behavior_cfg_t *b, int dt_ms, int64_t now_ms)
{
    float vx = 0, vy = 0, wz = 0;
    soul_state_t st;

    if (c->fault) {
        st = SOUL_FAULT;
        c->gaze_ms = 0;
    } else if (c->session != SOUL_IDLE) {
        st = c->session;            /* LISTEN / THINK / SPEAK — hold still */
        c->gaze_ms = 0;
    } else if (now_ms < c->gazed_until_ms) {
        st = SOUL_GAZED;            /* shying away after a sustained stare */
        vx = -SOUL_SLOW;
    } else if (!face->present) {
        st = SOUL_IDLE;
        c->gaze_ms = 0;
        if (b->idle_scan) {
            wz = 0.2f;              /* gentle look-around */
        }
    } else {
        float area = face->area_ratio;
        float center_wz = -SOUL_CENTER_KP * face->cx;  /* turn toward the face */
        if (area > b->near_hi) {
            st = SOUL_RETREAT;
            vx = -SOUL_SLOW;
            wz = center_wz;
            c->gaze_ms = 0;
        } else if (area < b->near_lo) {
            st = SOUL_APPROACH;
            vx = SOUL_SLOW;
            wz = center_wz;
            c->gaze_ms = 0;
        } else {
            st = SOUL_ENGAGE;
            wz = center_wz;
            if (face->frontal_score > b->frontal_thresh) {
                c->gaze_ms += dt_ms;
                if (c->gaze_ms >= (float)b->gaze_hold_ms) {
                    c->gazed_until_ms = now_ms + SOUL_GAZE_BACKOFF_MS;
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
    case SOUL_GAZED:   c->emotion = "o"; break;
    case SOUL_THINK:   c->emotion = "think"; break;
    case SOUL_SPEAK:
    case SOUL_LISTEN:  c->emotion = "waiting"; break;
    case SOUL_FAULT:   c->emotion = "sad"; break;
    default:           c->emotion = "waiting"; break;  /* IDLE/ENGAGE/APPROACH */
    }
}

/* ---- live wiring ---- */
static soul_ctx_t  s_ctx;
static const char *s_last_emotion;
static char        s_override_name[16];
static int64_t     s_override_until_ms;

static void apply_emotion(const char *emo, bool now_cut)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (now < s_override_until_ms && s_override_name[0]) {
        emo = s_override_name;
    }
    if (emo != s_last_emotion && (s_last_emotion == NULL || strcmp(emo, s_last_emotion) != 0)) {
        face_set_emotion(emo, now_cut ? FACE_NOW : FACE_FADE);
        telemetry_set_emotion(emo);
        s_last_emotion = emo;
    }
}

static void soul_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    soul_state_t last_state = SOUL_STATE_MAX;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SOUL_TICK_MS));
        int64_t now_ms = esp_timer_get_time() / 1000;

        tel_face_t face = {0};
        if (vision_ready()) {
            vision_get(&face);
        }

        soul_eval(&s_ctx, &face, &cfg->behavior, SOUL_TICK_MS, now_ms);

        if (s_ctx.state == SOUL_FAULT) {
            /* Hold on the FAULT source so teleop can't move a faulted robot. */
            motion_request(MOTION_SRC_FAULT, 0.0f, 0.0f, 0.0f, 250);
        } else {
            motion_set_intent(s_ctx.vx, s_ctx.vy, s_ctx.wz);
        }
        telemetry_set_soul(s_ctx.state);
        apply_emotion(s_ctx.emotion, s_ctx.state == SOUL_FAULT || s_ctx.state == SOUL_GAZED);

        if (s_ctx.state != last_state) {
            ns_evt_soul_t e = { .from = last_state, .to = s_ctx.state };
            telemetry_post(NS_EVT_SOUL_TRANSITION, &e, sizeof(e));
            if (s_ctx.state == SOUL_GAZED) {
                telemetry_post(NS_EVT_GAZED, NULL, 0);
            }
            last_state = s_ctx.state;
        }
    }
}

esp_err_t soul_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = SOUL_IDLE;
    s_ctx.session = SOUL_IDLE;
    s_ctx.emotion = "waiting";
    return ESP_OK;
}

esp_err_t soul_start(void)
{
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
    if (!name || !name[0]) {
        s_override_until_ms = 0;
        return;
    }
    strlcpy(s_override_name, name, sizeof(s_override_name));
    s_override_until_ms = esp_timer_get_time() / 1000 + ms;
    s_last_emotion = NULL;  /* force re-apply on next tick */
}
