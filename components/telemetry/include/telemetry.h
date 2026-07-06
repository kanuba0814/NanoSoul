#pragma once
/*
 * telemetry — the single shared state hub for the whole robot.
 *
 * One mutex-protected snapshot struct that every producer writes to via typed
 * setters, and every consumer (HUD overlay, WebSocket telemetry, serial log)
 * reads atomically via telemetry_get(). Plus a small event bus (esp_event) for
 * discrete happenings (soul transitions, wake, llm replies, faults) that the
 * companion link forwards to the PC.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(NANOSOUL_EVENT);

typedef enum {
    NS_EVT_SOUL_TRANSITION = 0, /* data: ns_evt_soul_t   */
    NS_EVT_FACE_PRESENT,        /* data: none            */
    NS_EVT_FACE_LOST,           /* data: none            */
    NS_EVT_GAZED,               /* data: none            */
    NS_EVT_WAKE,                /* data: none            */
    NS_EVT_LLM_REPLY,           /* data: ns_evt_text_t   */
    NS_EVT_FAULT,               /* data: ns_evt_text_t   */
    NS_EVT_SELFTEST_ITEM,       /* data: ns_evt_st_t     */
    /* --- interaction events (docs/12); data: none unless noted. Append only. */
    NS_EVT_TAP,                 /* IMU tap (single)      */
    NS_EVT_LIFTED,              /* picked up             */
    NS_EVT_PLACED,              /* set back down         */
    NS_EVT_DARK,                /* ambient went dark     */
    NS_EVT_BRIGHT,              /* ambient went bright   */
    NS_EVT_TOUCH,               /* screen touched        */
    NS_EVT_WHEEL_MOVED,         /* wheel pushed by hand  */
    NS_EVT_STALL,               /* data: ns_evt_text_t   */
    NS_EVT_LOUD,                /* sudden loud noise     */
} ns_event_id_t;

typedef enum {
    SOUL_IDLE = 0,
    SOUL_ENGAGE,
    SOUL_APPROACH,
    SOUL_RETREAT,
    SOUL_GAZED,
    SOUL_LISTEN,
    SOUL_THINK,
    SOUL_SPEAK,
    SOUL_FAULT,
    SOUL_LIFTED,   /* held aloft — motion frozen */
    SOUL_DOZE,     /* dozing in the dark         */
    SOUL_STATE_MAX,
} soul_state_t;

const char *soul_state_name(soul_state_t s);

typedef struct {
    soul_state_t from;
    soul_state_t to;
} ns_evt_soul_t;

typedef struct {
    char text[160];
} ns_evt_text_t;

typedef struct {
    char name[24];
    int  result;     /* st_result_t */
    char detail[64];
    int  round;
} ns_evt_st_t;

typedef struct {
    int count;       /* 1 = single tap, 2 = double tap */
} ns_evt_tap_t;

typedef struct {
    bool     present;
    float    cx, cy;        /* normalized offset from frame center, -1..1 */
    float    area_ratio;    /* bbox area / frame area, 0..1               */
    float    frontal_score; /* 0..1, higher = more frontal               */
    uint32_t ts_ms;
} tel_face_t;

typedef struct {
    float   vx, vy, wz;     /* body-frame velocity intent */
    int16_t duty[3];        /* computed per-wheel duty, signed -1023..1023 */
    bool    enabled;        /* motion actually driving the H-bridges? */
} tel_motion_t;

typedef struct {
    bool    present[3];
    int32_t count[3];
    float   rpm[3];
} tel_encoder_t;

typedef struct {
    char    activity[8];  /* "active"|"idle"|"locked"|"" (empty = never received) */
    int     idle_s;
    char    focus[8];     /* work/meeting/media/browse/comm/other/unknown */
    bool    media;
    bool    dnd;
    int64_t rx_ms;        /* device-side receive time (esp_timer ms), for staleness */
} tel_pc_t;

typedef struct {
    soul_state_t  soul;
    char          emotion[16];
    tel_face_t    face;
    tel_motion_t  motion;
    tel_encoder_t enc;
    bool          current_present;
    float         current_a;
    bool          net_up;
    char          ip[16];
    int8_t        rssi;
    char          llm[8];   /* "idle" / "busy" */
    char          voice[8]; /* "idle" / "listen" / "think" / "speak" */
    uint32_t      free_heap;
    uint32_t      free_psram;
    float         fps_render;
    float         fps_detect;
    tel_pc_t      pc;           /* PC-state fusion input (docs/12 §3.5) */
    char          beh[16];      /* current behavior / motion primitive label */
    float         mood_energy;  /* 0..1 */
    float         mood_social;  /* 0..1 */
} tel_snapshot_t;

esp_err_t telemetry_init(void);
void      telemetry_get(tel_snapshot_t *out);

void telemetry_set_soul(soul_state_t s);
void telemetry_set_emotion(const char *name);
void telemetry_set_face(const tel_face_t *f);
void telemetry_set_motion(const tel_motion_t *m);
void telemetry_set_encoder(const tel_encoder_t *e);
void telemetry_set_current(bool present, float amps);
void telemetry_set_net(bool up, const char *ip, int8_t rssi);
void telemetry_set_llm(const char *s);
void telemetry_set_voice(const char *s);
void telemetry_set_fps(float render, float detect);
void telemetry_set_pc(const tel_pc_t *pc);
void telemetry_set_beh(const char *name);
void telemetry_set_mood(float energy, float social);
void telemetry_refresh_perf(void); /* recompute free heap / psram */

esp_err_t telemetry_post(ns_event_id_t id, const void *data, size_t size);

#ifdef __cplusplus
}
#endif
