#include "simsense.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define OVR_MAX_VALS 5   /* OVR_FACE 是最宽的通道 */

typedef struct {
    bool    active;
    int64_t until_ms;      /* 绝对 ms；0 = 常驻（永不过期） */
    uint8_t n;
    float   v[OVR_MAX_VALS];
} ovr_slot_t;

static ovr_slot_t       s_tab[OVR_CH_MAX];
static SemaphoreHandle_t s_lock;

static const char *const s_names[OVR_CH_MAX] = {
    [OVR_LUX] = "lux",   [OVR_ACCEL] = "accel", [OVR_GYRO] = "gyro",
    [OVR_CURRENT_A] = "current_a", [OVR_ENC_RPM] = "enc_rpm", [OVR_FACE] = "face",
};

static const uint8_t s_lens[OVR_CH_MAX] = {
    [OVR_LUX] = 1, [OVR_ACCEL] = 3, [OVR_GYRO] = 3,
    [OVR_CURRENT_A] = 1, [OVR_ENC_RPM] = 3, [OVR_FACE] = 5,
};

size_t ovr_ch_len(ovr_ch_t ch)
{
    return (ch >= 0 && ch < OVR_CH_MAX) ? s_lens[ch] : 0;
}

esp_err_t simsense_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

esp_err_t override_set(ovr_ch_t ch, const float *v, size_t n, uint32_t ttl_ms)
{
    if (ch < 0 || ch >= OVR_CH_MAX || !v || n != s_lens[ch] || !s_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ovr_slot_t *s = &s_tab[ch];
    s->active = true;
    s->n = (uint8_t)n;
    memcpy(s->v, v, n * sizeof(float));
    s->until_ms = ttl_ms ? now_ms() + (int64_t)ttl_ms : 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void override_clear(ovr_ch_t ch)
{
    if (ch < 0 || ch >= OVR_CH_MAX || !s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_tab[ch].active = false;
    xSemaphoreGive(s_lock);
}

void override_clear_all(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < OVR_CH_MAX; i++) {
        s_tab[i].active = false;
    }
    xSemaphoreGive(s_lock);
}

bool override_apply(ovr_ch_t ch, float *inout, size_t n)
{
    if (ch < 0 || ch >= OVR_CH_MAX || !inout || !s_lock) {
        return false;
    }
    bool covered = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ovr_slot_t *s = &s_tab[ch];
    if (s->active) {
        if (s->until_ms && now_ms() > s->until_ms) {
            s->active = false;                 /* 惰性过期 */
        } else if (n == s->n) {
            memcpy(inout, s->v, n * sizeof(float));
            covered = true;
        }
    }
    xSemaphoreGive(s_lock);
    return covered;
}

bool override_face_apply(tel_face_t *f)
{
    if (!f) {
        return false;
    }
    float v[5];
    if (!override_apply(OVR_FACE, v, 5)) {
        return false;
    }
    f->present       = v[0] >= 0.5f;
    f->cx            = v[1];
    f->cy            = v[2];
    f->area_ratio    = v[3];
    f->frontal_score = v[4];
    f->ts_ms         = (uint32_t)now_ms();
    return true;
}

uint32_t override_mask(void)
{
    uint32_t m = 0;
    if (!s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t t = now_ms();
    for (int i = 0; i < OVR_CH_MAX; i++) {
        ovr_slot_t *s = &s_tab[i];
        if (s->active && (!s->until_ms || t <= s->until_ms)) {
            m |= (1u << i);
        }
    }
    xSemaphoreGive(s_lock);
    return m;
}

int ovr_ch_from_name(const char *name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < OVR_CH_MAX; i++) {
        if (strcmp(name, s_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *ovr_ch_name(ovr_ch_t ch)
{
    return (ch >= 0 && ch < OVR_CH_MAX) ? s_names[ch] : "";
}
