#pragma once
/*
 * voice — the spoken conversation pipeline.
 *
 *   wake  ->  LISTEN (record utterance, VAD-trimmed)
 *         ->  THINK  (STT -> cloud chat)
 *         ->  SPEAK  (TTS -> play), then back to perception.
 *
 * Half-duplex: the mic is not read while a reply is playing (avoids the speaker
 * feeding back into wake/VAD). soul state + face emotion track each stage.
 *
 * Wake source: this build uses an energy/VAD trigger (loud sustained speech near
 * the mic). The srmodel flash partition is reserved for ESP-SR WakeNet9
 * ("你好喵伴"); dropping WakeNet in only means replacing wake_detected() — the
 * rest of the pipeline is unchanged.
 */

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_init(i2c_master_bus_handle_t i2c_bus);
esp_err_t voice_start(void);
bool      voice_ready(void);

// Force a conversation turn now (companion/manual trigger, bypasses wake).
esp_err_t voice_trigger(void);

// Most recent mic-chunk RMS (for the mic_record selftest — the voice task owns
// the codec, so checks read this instead of touching it concurrently).
float voice_last_rms(void);

#ifdef __cplusplus
}
#endif
