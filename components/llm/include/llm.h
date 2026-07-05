#pragma once
/*
 * llm — cloud chat client, dual protocol. Speaks both the Anthropic Messages API
 * (POST /v1/messages, x-api-key, anthropic-version) and the OpenAI-compatible
 * chat completions API (POST /v1/chat/completions, Bearer). Which one is chosen
 * by chat.provider in the SD config; endpoint + key + model all live on the SD
 * card, never in the repo.
 *
 * Non-streaming, short replies (desktop companion). The cloud is for depth of
 * conversation only — it never enters the local perception/decision loop.
 */

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// One-shot chat: send user_msg, block for the reply (UTF-8) into `reply`.
// Returns ESP_OK on a normal reply; ESP_ERR_* on HTTP/parse error or refusal
// (in which case `reply` holds a short fallback line).
esp_err_t llm_chat(const char *user_msg, char *reply, size_t reply_cap);

// Speech-to-text (OpenAI-compatible /v1/audio/transcriptions). PCM is 16-bit
// mono at `sample_rate`; the recognized text lands in `text`. Uses the SD stt.*
// config (may point at a different provider than chat).
esp_err_t llm_stt(const int16_t *pcm, size_t samples, int sample_rate,
                  char *text, size_t text_cap);

// Text-to-speech (OpenAI-compatible /v1/audio/speech, wav). Returns a malloc'd
// 16-bit mono PCM buffer resampled to `out_rate` (caller frees *pcm_out). Uses
// the SD tts.* config.
esp_err_t llm_tts(const char *text, int out_rate, int16_t **pcm_out, size_t *samples_out);

#ifdef __cplusplus
}
#endif
