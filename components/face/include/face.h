#pragma once
/*
 * face — the robot's face: the emote animation player wired to the ST7701 panel.
 *
 * A thin executor over esp_emote_gen_player. It owns panel bring-up, the 90°
 * rotated flush (640x480 landscape emote canvas -> 480x640 portrait panel; the
 * panel is physically mounted rotated so the net result reads upright), the
 * emote_gen asset mount, and a switch queue + worker task so anim_fade never
 * runs inside the gfx render callback (that deadlocks — see esp_emote_gen_player
 * test_apps note).
 *
 * The decision state machine (soul) only ever calls face_set_emotion(); it never
 * touches the gfx runtime directly.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_NOW = 0,  // cut immediately
    FACE_FADE,     // let the current clip's segment plan drain, then switch
} face_switch_mode_t;

// Bring up panel + emote player, mount the emote_gen pack, start playing "waiting".
esp_err_t face_init(void);

// Queue an emotion switch by logical name (o/sad/sleep/think/waiting/...).
// Safe to call from any task, including gfx callbacks (it only enqueues).
esp_err_t face_set_emotion(const char *name, face_switch_mode_t mode);

// Set the top tip strip text (UTF-8), or NULL/"" to clear.
esp_err_t face_set_tip(const char *utf8);

// For the HUD overlay: the gfx display + handle to add debug labels onto.
gfx_disp_t   *face_gfx_disp(void);
gfx_handle_t  face_gfx_handle(void);

// Translucent HUD veil: pixels inside this canvas rect are dimmed + gray-tinted
// during flush so the overlay reads as see-through (labels draw on top). The HUD
// sets it to its panel area; clearing removes the veil.
void face_set_veil_rect(int x, int y, int w, int h);
void face_clear_veil(void);

// Rendered-frame counter (for HUD render-fps). Monotonic.
uint32_t face_frame_count(void);

// True once the player is up. Number of mounted emote clips, or -1 if not up.
bool face_ready(void);
int  face_emote_clip_count(void);

#ifdef __cplusplus
}
#endif
