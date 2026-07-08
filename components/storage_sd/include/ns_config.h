#pragma once
/*
 * NanoSoul runtime configuration — loaded from /sdcard/nanosoul/config.json.
 *
 * Every field has a built-in default; the SD file only overrides what it sets,
 * so a missing or partial file still boots. Real API keys live only on the SD
 * card, never in the repo (see sdcard_template/nanosoul/config.example.json).
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NS_CFG_STR      128
#define NS_CFG_PROMPT   384
#define NS_CONFIG_PATH  "/sdcard/nanosoul/config.json"

typedef struct {
    char ssid[33];
    char password[65];
} ns_wifi_cfg_t;

typedef struct {
    char provider[12];          /* "anthropic" | "openai" | "volc" */
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];
    char model[64];             /* room for e.g. doubao-seed-1-6-250615 / ep-... */
    int  max_tokens;
    char system_prompt[NS_CFG_PROMPT];
} ns_chat_cfg_t;

typedef struct {
    char provider[12];          /* "openai" | "volc" */
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];   /* openai bearer / volc 方舟专属 API Key (X-Api-Key) */
    char model[32];             /* openai model, or volc request model_name */
    char resource_id[48];       /* volc X-Api-Resource-Id (ASR: volc.seedasr.sauc.duration) */
} ns_stt_cfg_t;

typedef struct {
    char provider[12];          /* "openai" | "volc" */
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];   /* openai bearer / volc 方舟专属 API Key (X-Api-Key) */
    char model[32];
    char voice[48];             /* openai voice / volc speaker/voice_type (e.g. *_bigtts) */
    char resource_id[48];       /* volc X-Api-Resource-Id (TTS: seed-tts-2.0) */
} ns_tts_cfg_t;

typedef struct {
    float near_lo;         /* bbox area ratio below which we APPROACH  */
    float near_hi;         /* bbox area ratio above which we RETREAT   */
    float frontal_thresh;  /* frontal_score above which counts as gaze */
    int   gaze_hold_ms;    /* sustained gaze before GAZED fires        */
    int   gaze_cooldown_s; /* min gap before GAZED can re-fire         */
    bool  idle_scan;       /* slow scan sweep while IDLE               */
    bool  autonomy;        /* allow self-initiated idle behaviors      */
    int   autonomy_idle_s; /* idle time before autonomy may kick in    */
} ns_behavior_cfg_t;

typedef struct {
    bool  enabled;
    float energy_init;     /* 0..1 */
    float social_init;     /* 0..1 */
    int   social_tau_min;  /* social decay time constant, minutes      */
} ns_mood_cfg_t;

typedef struct {
    int  stale_s;          /* PC info older than this -> ignored        */
    bool respect_dnd;      /* honor the manual do-not-disturb flag      */
    bool quiet_work;       /* focus=work + active -> QUIET (no invite)  */
    bool quiet_meeting;    /* focus=meeting -> SILENT (hold still)      */
    int  invite_idle_s;    /* PC idle this long + face -> may invite    */
    int  invite_cooldown_min; /* min gap between invitations            */
} ns_pc_cfg_t;

typedef struct {
    int dark_lux;          /* lux below this (held) -> DARK  */
    int bright_lux;        /* lux above this -> BRIGHT       */
    int dark_hold_s;       /* dark must persist this long    */
} ns_light_cfg_t;

typedef struct {
    float tap_th;          /* jerk magnitude (m/s^2) for a tap */
    float lift_g_dev;      /* |mag-1g| fraction to call lifted */
    int   lift_hold_ms;    /* deviation must persist this long */
    int   place_still_ms;  /* stillness before PLACED          */
    int   tilt_deg;        /* pitch/roll beyond this -> tilted  */
} ns_imu_cfg_t;

typedef struct {
    int stall_ma;          /* motor current above this = candidate stall */
    int stall_ms;          /* stall condition must persist this long     */
    int stall_retry;       /* auto-recovery attempts before locking out  */
} ns_protect_cfg_t;

typedef struct {
    int approach_budget_cm; /* max travel per APPROACH before giving up */
    int nudge_cm;           /* nominal nudge step size                  */
    int ramp_ms;            /* primitive ramp in/out                    */
    int jitter_pct;         /* amplitude jitter                         */
} ns_move_cfg_t;

typedef struct {
    bool enabled;          /* actually drive motors?      */
    int  max_duty_pct;     /* clamp on computed duty, 0-100 */
} ns_motion_cfg_t;

typedef struct {
    bool enabled;
    char token[64];
} ns_companion_cfg_t;

typedef struct {
    int volume;            /* speaker output volume, 0-100 */
} ns_audio_cfg_t;

typedef struct {
    bool overlay;          /* draw debug HUD over the face */
    char log_level[8];
} ns_debug_cfg_t;

typedef struct {
    ns_wifi_cfg_t      wifi;
    ns_chat_cfg_t      chat;
    ns_stt_cfg_t       stt;
    ns_tts_cfg_t       tts;
    ns_behavior_cfg_t  behavior;
    ns_mood_cfg_t      mood;
    ns_pc_cfg_t        pc;
    ns_light_cfg_t     light;
    ns_imu_cfg_t       imu;
    ns_protect_cfg_t   protect;
    ns_move_cfg_t      move;
    ns_motion_cfg_t    motion;
    ns_companion_cfg_t companion;
    ns_audio_cfg_t     audio;
    ns_debug_cfg_t     debug;
    char               source[8]; /* "default" | "sd" */
} ns_config_t;

/* Fill `cfg` with built-in defaults (never fails). */
void ns_config_defaults(ns_config_t *cfg);

/* Load defaults, then overlay any fields present in `path`. Returns ESP_OK
 * even if the file is absent (cfg stays at defaults, source="default"). */
esp_err_t ns_config_load(const char *path, ns_config_t *cfg);

/* Load into the process-global config and return it. */
esp_err_t ns_config_init(const char *path);
const ns_config_t *ns_config_get(void);

#ifdef __cplusplus
}
#endif
