#pragma once
/*
 * dialog — one chat turn, orchestrated. Ties the cloud LLM to the robot's face:
 * puts the soul into THINK (think emotion), runs the chat request off the caller
 * task, shows the reply on the tip strip, and emits NS_EVT_LLM_REPLY for the
 * companion link. Shared by the voice pipeline and the companion "ask" command.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dialog_init(void);

// Queue one chat turn (non-blocking). The reply lands on the face tip + as an
// NS_EVT_LLM_REPLY event.
esp_err_t dialog_ask(const char *text);

#ifdef __cplusplus
}
#endif
