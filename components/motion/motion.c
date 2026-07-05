#include "motion.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
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

void motion_set_intent(float vx, float vy, float wz)
{
    int16_t duty[3];
    motion_ik(vx, vy, wz, s_max_duty_pct, duty);

    tel_motion_t m = {
        .vx = vx, .vy = vy, .wz = wz,
        .duty = { duty[0], duty[1], duty[2] },
        .enabled = s_enabled,
    };
    telemetry_set_motion(&m);

    if (s_enabled && s_apply) {
        if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
        s_apply(duty);
        if (s_lock) xSemaphoreGive(s_lock);
    }
}

void motion_stop(void)
{
    motion_set_intent(0.0f, 0.0f, 0.0f);
}
