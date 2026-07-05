#include "telemetry.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

ESP_EVENT_DEFINE_BASE(NANOSOUL_EVENT);

static const char *TAG = "telemetry";

static tel_snapshot_t   s_snap;
static SemaphoreHandle_t s_lock;

static const char *const s_soul_names[SOUL_STATE_MAX] = {
    "IDLE", "ENGAGE", "APPROACH", "RETREAT", "GAZED",
    "LISTEN", "THINK", "SPEAK", "FAULT",
};

const char *soul_state_name(soul_state_t s)
{
    if (s < 0 || s >= SOUL_STATE_MAX) {
        return "?";
    }
    return s_soul_names[s];
}

static inline void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static inline void unlock(void) { xSemaphoreGive(s_lock); }

esp_err_t telemetry_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.soul = SOUL_IDLE;
    strcpy(s_snap.emotion, "waiting");
    strcpy(s_snap.llm, "idle");
    strcpy(s_snap.voice, "idle");

    /* The default event loop is shared with esp_netif/wifi; tolerate a prior
     * create by another subsystem. */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "default event loop create: %s", esp_err_to_name(err));
    }

    telemetry_refresh_perf();
    return ESP_OK;
}

void telemetry_get(tel_snapshot_t *out)
{
    if (!out) {
        return;
    }
    lock();
    *out = s_snap;
    unlock();
}

void telemetry_set_soul(soul_state_t s)
{
    lock();
    s_snap.soul = s;
    unlock();
}

void telemetry_set_emotion(const char *name)
{
    lock();
    if (name) {
        strlcpy(s_snap.emotion, name, sizeof(s_snap.emotion));
    }
    unlock();
}

void telemetry_set_face(const tel_face_t *f)
{
    if (!f) {
        return;
    }
    lock();
    s_snap.face = *f;
    unlock();
}

void telemetry_set_motion(const tel_motion_t *m)
{
    if (!m) {
        return;
    }
    lock();
    s_snap.motion = *m;
    unlock();
}

void telemetry_set_encoder(const tel_encoder_t *e)
{
    if (!e) {
        return;
    }
    lock();
    s_snap.enc = *e;
    unlock();
}

void telemetry_set_current(bool present, float amps)
{
    lock();
    s_snap.current_present = present;
    s_snap.current_a = amps;
    unlock();
}

void telemetry_set_net(bool up, const char *ip, int8_t rssi)
{
    lock();
    s_snap.net_up = up;
    s_snap.rssi = rssi;
    strlcpy(s_snap.ip, ip ? ip : "", sizeof(s_snap.ip));
    unlock();
}

void telemetry_set_llm(const char *s)
{
    lock();
    strlcpy(s_snap.llm, s ? s : "idle", sizeof(s_snap.llm));
    unlock();
}

void telemetry_set_voice(const char *s)
{
    lock();
    strlcpy(s_snap.voice, s ? s : "idle", sizeof(s_snap.voice));
    unlock();
}

void telemetry_set_fps(float render, float detect)
{
    lock();
    s_snap.fps_render = render;
    s_snap.fps_detect = detect;
    unlock();
}

void telemetry_refresh_perf(void)
{
    uint32_t heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    lock();
    s_snap.free_heap = heap;
    s_snap.free_psram = psram;
    unlock();
}

esp_err_t telemetry_post(ns_event_id_t id, const void *data, size_t size)
{
    return esp_event_post(NANOSOUL_EVENT, (int32_t)id, data, size, pdMS_TO_TICKS(50));
}
