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
    char provider[12];          /* "anthropic" | "openai" */
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];
    char model[48];
    int  max_tokens;
    char system_prompt[NS_CFG_PROMPT];
} ns_chat_cfg_t;

typedef struct {
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];
    char model[32];
} ns_stt_cfg_t;

typedef struct {
    char base_url[NS_CFG_STR];
    char api_key[NS_CFG_STR];
    char model[32];
    char voice[16];
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
    bool enabled;          /* actually drive motors?      */
    int  max_duty_pct;     /* clamp on computed duty, 0-100 */
} ns_motion_cfg_t;

typedef struct {
    bool enabled;
    char token[64];
} ns_companion_cfg_t;

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
    ns_motion_cfg_t    motion;
    ns_companion_cfg_t companion;
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
