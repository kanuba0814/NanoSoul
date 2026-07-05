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

#ifdef __cplusplus
}
#endif
