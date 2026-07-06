#include "testlink.h"

#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "soul.h"
#include "telemetry.h"

static const char *TAG = "testlink";

/* ---------------- sink registry ---------------- */

#define NS_MAX_SINKS 4
static ns_sink_fn        s_sinks[NS_MAX_SINKS];
static SemaphoreHandle_t s_sink_lock;

static void sink_lock_init(void)
{
    if (!s_sink_lock) {
        s_sink_lock = xSemaphoreCreateMutex();
    }
}

esp_err_t ns_link_add_sink(ns_sink_fn fn)
{
    if (!fn) {
        return ESP_ERR_INVALID_ARG;
    }
    sink_lock_init();
    xSemaphoreTake(s_sink_lock, portMAX_DELAY);
    for (int i = 0; i < NS_MAX_SINKS; i++) {
        if (s_sinks[i] == fn) {           /* idempotent */
            xSemaphoreGive(s_sink_lock);
            return ESP_OK;
        }
    }
    for (int i = 0; i < NS_MAX_SINKS; i++) {
        if (!s_sinks[i]) {
            s_sinks[i] = fn;
            xSemaphoreGive(s_sink_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_sink_lock);
    return ESP_ERR_NO_MEM;
}

void ns_link_broadcast(const char *json)
{
    if (!json || !s_sink_lock) {
        return;
    }
    /* snapshot the sink list under lock, call outside to avoid holding it during I/O */
    ns_sink_fn local[NS_MAX_SINKS];
    xSemaphoreTake(s_sink_lock, portMAX_DELAY);
    memcpy(local, s_sinks, sizeof(local));
    xSemaphoreGive(s_sink_lock);
    for (int i = 0; i < NS_MAX_SINKS; i++) {
        if (local[i]) {
            local[i](json);
        }
    }
}

/* ---------------- 1 Hz state heartbeat ---------------- */

char *ns_build_state_json(void)
{
    tel_snapshot_t t;
    telemetry_get(&t);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "v", 1);
    cJSON_AddStringToObject(r, "type", "state");
    cJSON_AddNumberToObject(r, "ts", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(r, "soul", soul_state_name(t.soul));
    cJSON_AddStringToObject(r, "emotion", t.emotion);

    cJSON *face = cJSON_AddObjectToObject(r, "face");
    cJSON_AddBoolToObject(face, "present", t.face.present);
    cJSON_AddNumberToObject(face, "cx", t.face.cx);
    cJSON_AddNumberToObject(face, "cy", t.face.cy);
    cJSON_AddNumberToObject(face, "area", t.face.area_ratio);
    cJSON_AddNumberToObject(face, "frontal", t.face.frontal_score);

    cJSON *mo = cJSON_AddObjectToObject(r, "motion");
    cJSON *intent = cJSON_AddArrayToObject(mo, "intent");
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.vx));
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.vy));
    cJSON_AddItemToArray(intent, cJSON_CreateNumber(t.motion.wz));
    cJSON *duty = cJSON_AddArrayToObject(mo, "duty");
    for (int i = 0; i < 3; i++) {
        cJSON_AddItemToArray(duty, cJSON_CreateNumber(t.motion.duty[i]));
    }
    cJSON_AddBoolToObject(mo, "enabled", t.motion.enabled);

    cJSON *enc = cJSON_AddArrayToObject(r, "enc_rpm");
    for (int i = 0; i < 3; i++) {
        cJSON_AddItemToArray(enc, cJSON_CreateNumber((int)t.enc.rpm[i]));
    }
    if (t.current_present) {
        cJSON_AddNumberToObject(r, "current_ma", (int)(t.current_a * 1000));
    } else {
        cJSON_AddNullToObject(r, "current_ma");
    }
    cJSON *net = cJSON_AddObjectToObject(r, "net");
    cJSON_AddStringToObject(net, "ip", t.net_up ? t.ip : "");
    cJSON_AddNumberToObject(net, "rssi", t.rssi);
    cJSON_AddStringToObject(r, "llm", t.llm);
    cJSON_AddStringToObject(r, "voice", t.voice);
    cJSON_AddNumberToObject(r, "heap", t.free_heap);
    cJSON *fps = cJSON_AddObjectToObject(r, "fps");
    cJSON_AddNumberToObject(fps, "render", t.fps_render);
    cJSON_AddNumberToObject(fps, "detect", t.fps_detect);

    cJSON_AddStringToObject(r, "beh", t.beh);
    cJSON *mood = cJSON_AddObjectToObject(r, "mood");
    cJSON_AddNumberToObject(mood, "energy", t.mood_energy);
    cJSON_AddNumberToObject(mood, "social", t.mood_social);
    if (t.pc.activity[0]) {
        cJSON *pc = cJSON_AddObjectToObject(r, "pc");
        cJSON_AddStringToObject(pc, "activity", t.pc.activity);
        cJSON_AddNumberToObject(pc, "idle_s", t.pc.idle_s);
        cJSON_AddStringToObject(pc, "focus", t.pc.focus);
        cJSON_AddBoolToObject(pc, "media", t.pc.media);
        cJSON_AddBoolToObject(pc, "dnd", t.pc.dnd);
    } else {
        cJSON_AddNullToObject(r, "pc");
    }

    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}

static void push_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        char *s = ns_build_state_json();
        if (s) {
            ns_link_broadcast(s);
            free(s);
        }
    }
}

/* ---------------- telemetry event bus → event frames ---------------- */

static void emit_event(const char *name, cJSON *data /*takes ownership*/)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "v", 1);
    cJSON_AddStringToObject(r, "type", "event");
    cJSON_AddNumberToObject(r, "ts", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(r, "name", name);
    cJSON_AddItemToObject(r, "data", data ? data : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (s) {
        ns_link_broadcast(s);
        free(s);
    }
}

static void on_ns_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch ((ns_event_id_t)id) {
    case NS_EVT_SOUL_TRANSITION: {
        ns_evt_soul_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "from", soul_state_name(e->from));
        cJSON_AddStringToObject(d, "to", soul_state_name(e->to));
        emit_event("soul_transition", d);
        break;
    }
    case NS_EVT_FACE_PRESENT: emit_event("face_present", NULL); break;
    case NS_EVT_FACE_LOST:    emit_event("face_lost", NULL); break;
    case NS_EVT_GAZED:        emit_event("gazed", NULL); break;
    case NS_EVT_WAKE:         emit_event("wake", NULL); break;
    case NS_EVT_LLM_REPLY: {
        ns_evt_text_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", e->text);
        emit_event("llm_reply", d);
        break;
    }
    case NS_EVT_FAULT: {
        ns_evt_text_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", e->text);
        emit_event("fault", d);
        break;
    }
    /* --- interaction events (docs/12) --- */
    case NS_EVT_TAP:         emit_event("tap", NULL); break;
    case NS_EVT_LIFTED:      emit_event("lifted", NULL); break;
    case NS_EVT_PLACED:      emit_event("placed", NULL); break;
    case NS_EVT_DARK:        emit_event("dark", NULL); break;
    case NS_EVT_BRIGHT:      emit_event("bright", NULL); break;
    case NS_EVT_TOUCH: {
        ns_evt_touch_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddBoolToObject(d, "long", e && e->long_press);
        emit_event("touch", d);
        break;
    }
    case NS_EVT_WHEEL_MOVED: emit_event("wheel_moved", NULL); break;
    case NS_EVT_LOUD:        emit_event("loud", NULL); break;
    case NS_EVT_STALL: {
        ns_evt_text_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", e ? e->text : "");
        emit_event("stall", d);
        break;
    }
    /* selftest matrix: forward each item so the test host can render it live
     * (companion previously dropped this — the selftest command was a no-op). */
    case NS_EVT_SELFTEST_ITEM: {
        ns_evt_st_t *e = data;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name", e->name);
        cJSON_AddNumberToObject(d, "result", e->result);
        cJSON_AddStringToObject(d, "detail", e->detail);
        cJSON_AddNumberToObject(d, "round", e->round);
        emit_event("selftest_item", d);
        break;
    }
    default: break;
    }
}

/* ---------------- shared core start (idempotent) ---------------- */

esp_err_t testlink_core_start(void)
{
    static bool started;
    if (started) {
        return ESP_OK;
    }
    sink_lock_init();
    esp_err_t err = esp_event_handler_instance_register(
        NANOSOUL_EVENT, ESP_EVENT_ANY_ID, on_ns_event, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event register failed: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreatePinnedToCore(push_task, "tl_state", 6144, NULL, 3, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }
    started = true;
    ESP_LOGI(TAG, "core up (state push + event forward)");
    return ESP_OK;
}
