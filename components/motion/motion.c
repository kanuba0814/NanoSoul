#include "motion.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "telemetry.h"

static const char *TAG = "motion";

/* Wheel mounting angles around the body (deg), CCW from +x (forward).
 * 120° apart. Confirm the M0/M1/M2 -> angle assignment against the chassis. */
static const float WHEEL_ANGLE_DEG[3] = { 0.0f, 120.0f, 240.0f };

static int              s_max_duty_pct = 40;
static volatile bool    s_enabled;
static motion_apply_fn  s_apply;
static SemaphoreHandle_t s_lock;

/* s_lock protects the slot array, the winner selection, and the s_apply() call
 * as one critical section. telemetry_set_motion() uses telemetry's own mutex,
 * so calling it while holding s_lock cannot deadlock. */
static motion_slot_t      s_slot[MOTION_SRC_MAX];
static esp_timer_handle_t s_lease_timer;

void motion_ik(float vx, float vy, float wz, int max_duty_pct, int16_t duty[3])
{
    float w[3];
    float peak = 0.0f;
    for (int i = 0; i < 3; i++) {
        float a = WHEEL_ANGLE_DEG[i] * (float)M_PI / 180.0f;
        // wheel drive dir is tangent (angle+90°): v·dir + rotation
        w[i] = vx * (-sinf(a)) + vy * (cosf(a)) + wz;
        float m = fabsf(w[i]);
        if (m > peak) {
            peak = m;
        }
    }
    // normalize so the fastest wheel never clips (preserves heading)
    if (peak > 1.0f) {
        for (int i = 0; i < 3; i++) {
            w[i] /= peak;
        }
    }
    float scale = (float)MOTION_DUTY_MAX * (float)max_duty_pct / 100.0f;
    for (int i = 0; i < 3; i++) {
        float d = w[i] * scale;
        if (d > MOTION_DUTY_MAX) d = MOTION_DUTY_MAX;
        if (d < -MOTION_DUTY_MAX) d = -MOTION_DUTY_MAX;
        duty[i] = (int16_t)lroundf(d);
    }
}

/* ---- arbiter ---- */

motion_src_t motion_arbitrate(const motion_slot_t slots[MOTION_SRC_MAX], int64_t now_ms)
{
    for (int i = 0; i < MOTION_SRC_MAX; i++) {
        if (slots[i].deadline_ms > now_ms) {
            return (motion_src_t)i;
        }
    }
    return MOTION_SRC_MAX;
}

/* Compute IK + publish telemetry + drive. Caller must hold s_lock. */
static void drive_locked(float vx, float vy, float wz)
{
    int16_t duty[3];
    motion_ik(vx, vy, wz, s_max_duty_pct, duty);

    tel_motion_t m = {
        .vx = vx, .vy = vy, .wz = wz,
        .duty = { duty[0], duty[1], duty[2] },
        .enabled = s_enabled,
    };
    telemetry_set_motion(&m);   /* telemetry's own mutex — no deadlock with s_lock */

    if (s_enabled && s_apply) {
        s_apply(duty);
    }
}

/* Select the winning source and drive it. Caller must hold s_lock. */
static void apply_winner_locked(int64_t now_ms)
{
    motion_src_t win = motion_arbitrate(s_slot, now_ms);
    if (win == MOTION_SRC_MAX) {
        drive_locked(0.0f, 0.0f, 0.0f);
    } else {
        drive_locked(s_slot[win].vx, s_slot[win].vy, s_slot[win].wz);
    }
}

static void lease_timer_cb(void *arg)
{
    (void)arg;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    apply_winner_locked(esp_timer_get_time() / 1000);
    if (s_lock) xSemaphoreGive(s_lock);
}

esp_err_t motion_init(int max_duty_pct, bool enabled)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_max_duty_pct = (max_duty_pct > 0 && max_duty_pct <= 100) ? max_duty_pct : 40;
    s_enabled = enabled;
    ESP_LOGI(TAG, "motion IK ready (max_duty %d%%, drive %s)",
             s_max_duty_pct, enabled ? "ENABLED" : "compute-only");
    motion_stop();

    if (!s_lease_timer) {
        const esp_timer_create_args_t args = {
            .callback = lease_timer_cb,
            .name = "motion_lease",
        };
        if (esp_timer_create(&args, &s_lease_timer) == ESP_OK) {
            esp_timer_start_periodic(s_lease_timer, 100 * 1000);   /* 100 ms */
        } else {
            ESP_LOGW(TAG, "lease timer create failed — leases won't auto-expire");
        }
    }
    return ESP_OK;
}

void motion_set_apply(motion_apply_fn fn) { s_apply = fn; }
bool motion_enabled(void)                 { return s_enabled; }

void motion_set_enabled(bool on)
{
    s_enabled = on;
    if (!on) {
        motion_stop();
    }
}

void motion_request(motion_src_t src, float vx, float vy, float wz, uint32_t ttl_ms)
{
    if (src >= MOTION_SRC_MAX) {
        return;
    }
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time() / 1000;
    s_slot[src].vx = vx;
    s_slot[src].vy = vy;
    s_slot[src].wz = wz;
    s_slot[src].deadline_ms = now + ttl_ms;
    apply_winner_locked(now);
    if (s_lock) xSemaphoreGive(s_lock);
}

void motion_set_intent(float vx, float vy, float wz)
{
    motion_request(MOTION_SRC_BEHAVIOR, vx, vy, wz, 250);
}

void motion_stop(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_slot, 0, sizeof(s_slot));
    drive_locked(0.0f, 0.0f, 0.0f);
    if (s_lock) xSemaphoreGive(s_lock);
}
