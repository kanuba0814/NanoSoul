#include "app_sense.h"

#include <math.h>
#include <stdlib.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c1.h"
#include "drv_encoder.h"
#include "drv_ina219.h"
#include "drv_motor.h"
#include "motion.h"
#include "ns_config.h"
#include "simsense.h"
#include "soul.h"
#include "telemetry.h"

#define WHEEL_MOVED_COUNTS 826   /* ~1/4 output-shaft rev (3304/4), in motor-axis counts */
#define STALL_DUTY_MIN     307   /* ~30% of 1023: only judge a wheel that's driven hard */
#define STALL_RPM_MAX      5.0f  /* below this = not turning */

/* ---- stall protection FSM (pure) ---- */

stall_action_t stall_step(stall_fsm_t *s, bool cond, int stall_ms,
                          int retry_gap_ms, int max_retry, int64_t now_ms)
{
    if (!s->tripped) {
        if (cond) {
            if (s->cond_since_ms == 0) {
                s->cond_since_ms = now_ms;
            } else if (now_ms - s->cond_since_ms >= stall_ms) {
                s->tripped = true;
                s->cond_since_ms = 0;
                s->next_try_ms = now_ms + retry_gap_ms;
                return STALL_TRIP;
            }
        } else {
            s->cond_since_ms = 0;
        }
        return STALL_OK;
    }
    /* tripped: wait out the retry gap, then try again or give up */
    if (now_ms >= s->next_try_ms) {
        if (s->tries < max_retry) {
            s->tries++;
            s->tripped = false;
            s->cond_since_ms = now_ms;   /* re-arm: needs stall_ms again to re-trip */
            s->next_try_ms = now_ms + retry_gap_ms;
            return STALL_RETRY;
        }
        return STALL_GIVEUP;
    }
    return STALL_OK;
}

void stall_reset(stall_fsm_t *s)
{
    s->tripped = false;
    s->cond_since_ms = 0;
    s->tries = 0;
    s->next_try_ms = 0;
}

static bool stall_cond(const tel_snapshot_t *t, int stall_ma)
{
    if (!t->current_present || (int)(t->current_a * 1000) < stall_ma) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (abs(t->motion.duty[i]) > STALL_DUTY_MIN && fabsf(t->enc.rpm[i]) < STALL_RPM_MAX) {
            return true;
        }
    }
    return false;
}

static void sense_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    int         base[3] = {0};
    bool        have_base = false;
    stall_fsm_t stall = {0};

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));   /* 10 Hz */

        tel_encoder_t e = {0};
        float rpms[3];
        for (int i = 0; i < 3; i++) {
            e.present[i] = true;
            e.count[i]   = encoder_count(i);
            rpms[i]      = encoder_rpm(i);
        }
        /* 测试注入：覆盖转速（count 不覆盖，手推轮判定用真值）。 */
        override_apply(OVR_ENC_RPM, rpms, 3);
        for (int i = 0; i < 3; i++) {
            e.rpm[i] = rpms[i];
        }
        telemetry_set_encoder(&e);

        /* 测试注入：覆盖电机电流。覆盖时必须强制 present=true，否则堵转判定
         * (stall_cond 先看 current_present) 直接短路，注入的电流测不到。 */
        float cur = ina219_present() ? ina219_current_a() : 0.0f;
        bool  cur_covered = override_apply(OVR_CURRENT_A, &cur, 1);
        telemetry_set_current(ina219_present() || cur_covered, cur);

        /* WHEEL_MOVED: encoder motion while we are NOT driving = pushed by hand. */
        tel_snapshot_t t;
        telemetry_get(&t);
        bool not_driving = !t.motion.enabled ||
                           (t.motion.duty[0] == 0 && t.motion.duty[1] == 0 && t.motion.duty[2] == 0);
        if (!have_base) {
            for (int i = 0; i < 3; i++) base[i] = e.count[i];
            have_base = true;
        }
        if (not_driving) {
            for (int i = 0; i < 3; i++) {
                if (abs(e.count[i] - base[i]) > WHEEL_MOVED_COUNTS) {
                    telemetry_post(NS_EVT_WHEEL_MOVED, NULL, 0);
                    base[i] = e.count[i];
                }
            }
        } else {
            for (int i = 0; i < 3; i++) base[i] = e.count[i];   /* reset baseline while driving */
        }

        /* Stall protection (docs/12 S14). Only meaningful when driving wheels. */
        if (motion_enabled()) {
            int64_t now = esp_timer_get_time() / 1000;
            bool cond = stall_cond(&t, cfg->protect.stall_ma);
            stall_action_t act = stall_step(&stall, cond, cfg->protect.stall_ms,
                                            3000, cfg->protect.stall_retry, now);
            if (act == STALL_TRIP) {
                motors_enable(false);
                soul_notify_fault("stall");
                ns_evt_text_t ev = { .text = "stall" };
                telemetry_post(NS_EVT_STALL, &ev, sizeof(ev));
            } else if (act == STALL_RETRY) {
                motors_enable(true);   /* re-arm; if it still stalls it re-trips */
            }
            /* Fault cleared elsewhere (touch long / companion) -> reset + re-enable */
            if (t.soul != SOUL_FAULT && stall.tripped) {
                stall_reset(&stall);
                motors_enable(true);
            }
        } else {
            stall_reset(&stall);
        }
    }
}

esp_err_t app_sense_start(void)
{
    encoders_init();                     /* PCNT; harmless if wheels unwired */
    ina219_init(board_i2c1_bus());       /* shares the off-board I2C1 bus */
    return xTaskCreatePinnedToCore(sense_task, "sense", 3072, NULL, 4, NULL, 0) == pdPASS
               ? ESP_OK : ESP_FAIL;
}
