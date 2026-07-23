#pragma once
/*
 * audio — ES8311 codec + NS4150B amp, full-duplex I2S (record + play).
 *
 * 16 kHz / 16-bit / mono — the format ESP-SR and the STT/TTS endpoints want.
 * Playback drives the PA enable (GPIO53). The voice pipeline runs half-duplex:
 * it mutes the mic while a TTS reply is playing to avoid the speaker feeding
 * back into wake detection.
 *
 * NOTE: the record path is unverified on this board (playback is proven from the
 * testP4 bring-up). It is the first thing to confirm on the bench.
 */

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE 16000

esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus);
bool      audio_ready(void);

// Blocking playback of `samples` int16 mono samples (enables the PA around it).
esp_err_t audio_play(const int16_t *pcm, size_t samples);

// Blocking capture of `samples` int16 mono samples from the mic.
esp_err_t audio_record(int16_t *pcm, size_t samples);

// Short three-note chime — plays on boot to confirm the DAC + speaker path.
esp_err_t audio_boot_chime(void);

// Two descending tones — audible cue that a voice turn failed (no net / STT / TTS).
esp_err_t audio_fail_tone(void);

void      audio_set_volume(int pct);

#ifdef __cplusplus
}
#endif
