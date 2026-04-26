#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PLAYER_PATH_MAX 512

typedef enum {
    AUDIO_PLAYER_STATE_IDLE = 0,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSED,
    AUDIO_PLAYER_STATE_ERROR,
} audio_player_state_t;

typedef enum {
    AUDIO_PLAYER_EVENT_STARTED = 0,
    AUDIO_PLAYER_EVENT_FINISHED,
    AUDIO_PLAYER_EVENT_ERROR,
} audio_player_event_type_t;

typedef struct {
    audio_player_event_type_t type;
    esp_err_t err;
    char path[AUDIO_PLAYER_PATH_MAX];
} audio_player_event_t;

esp_err_t audio_player_init(void);
esp_err_t audio_player_request_play(const char *path);
esp_err_t audio_player_request_pause(void);
esp_err_t audio_player_request_resume(void);
esp_err_t audio_player_request_stop(void);
audio_player_state_t audio_player_get_state(void);
bool audio_player_take_event(audio_player_event_t *event, TickType_t timeout);
esp_err_t audio_player_play_test_tone(uint32_t freq_hz, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
