#pragma once
/*
 * hud — debug overlay drawn on top of the emote face.
 *
 * A handful of gfx text labels pinned to the top of the emote canvas, refreshed
 * ~2 Hz from the telemetry snapshot: soul state, face bbox, motor intent/duty,
 * encoder rpm, current, net, llm/voice, fps, heap. This is the "debug build"
 * surface — it makes every computed hardware/decision parameter visible on the
 * screen even when a subsystem (motors, encoders) is not wired.
 *
 * Labels live on the same gfx display as the animation, so they rotate with it
 * and read upright on the physically-rotated panel.
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Create the overlay labels on the face's gfx display. Call after face_init().
esp_err_t hud_init(void);

// Spawn the ~2 Hz refresh task.
esp_err_t hud_start(void);

// Show/hide the overlay at runtime (companion set_overlay command, config).
void hud_set_enabled(bool on);

// True once labels are created and the refresh task is running.
bool hud_ready(void);

#ifdef __cplusplus
}
#endif
