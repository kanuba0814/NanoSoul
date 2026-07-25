#pragma once
/*
 * voice — the spoken conversation pipeline.
 *
 *   neural wake -> wake chime -> LISTEN, or
 *   energy VAD  -> silent record -> STT text wake gate -> wake chime
 *   then THINK (cloud chat) -> SPEAK (TTS), followed by active follow-up.
 *
 * Half-duplex: the mic is not read while a reply is playing (avoids the speaker
 * feeding back into detection). soul state + face emotion track each stage.
 *
 * Wake engine is a build-time choice (Kconfig NS_WAKE_WORD_*):
 *   MN_XIAOWANG (default) — ESP-SR AFE + MultiNet7-cn always-on command
 *       "xiao wang xiao wang" (小王小王). Official model, no training;
 *       detection runs on-device, the cloud is only asked after a wake.
 *   WN_MIAOBAN — ESP-SR AFE + WakeNet9 wn9_nihaomiaoban_tts2 (你好喵伴).
 *       Device-side wake is the addressing proof, so the STT text gate is
 *       bypassed in this build.
 *   VAD — energy recording trigger fallback. It stays silent until cloud ASR
 *       confirms the text contains "小王", then chimes and answers.
 * SR models live in the "model" flash partition (esp-sr CMake packs and flashes
 * them with `idf.py flash`). After a reply the pipeline listens actively for
 * the rest of follow_window_s, so the wake word is only needed once per chat.
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

/* Speak a short canned phrase via cloud TTS at the next idle moment (queued,
 * one at a time; silently dropped offline). For event reactions (认主问候),
 * not conversation. */
esp_err_t voice_say(const char *text);

// Most recent mic-chunk RMS (for the mic_record selftest — the voice task owns
// the codec, so checks read this instead of touching it concurrently).
float voice_last_rms(void);

/* Which wake engine this build runs: "multinet(小王小王)", "wakenet(你好喵伴)"
 * or "energy-vad" — for the boot log, telemetry and the wakenet_load selftest. */
const char *voice_wake_engine(void);

/* SR engine (AFE + model) initialized — false on VAD builds or model-load
 * failure (e.g. srmodels.bin not flashed to the model partition). */
bool voice_sr_ready(void);

/* SR engine lifecycle: 0=NONE(VAD build) 1=INITING(model loading, ~16s)
 * 2=READY 3=FAILED — the wakenet_load selftest distinguishes "still loading"
 * from "actually broken". */
int voice_sr_state(void);

#ifdef __cplusplus
}
#endif
