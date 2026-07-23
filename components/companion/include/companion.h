#pragma once
/*
 * companion — the PC-side interface: an esp_http_server WebSocket endpoint
 * (/ws) implementing the frozen protocol in docs/09. Pushes robot + user state
 * (1 Hz `state` heartbeat + immediate `event` frames sourced from the telemetry
 * event bus) so the desktop companion app can see the user's presence, gaze,
 * interactions and emotion flow; accepts commands (ask / set_emotion / teleop /
 * get_snapshot / estop / ...).
 *
 * LAN only, token-authenticated (companion.token from the SD config).
 */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the WS server + push task (no-op if companion.enabled is false).
esp_err_t companion_start(void);

// True once the WS server is listening (for the ws_loopback selftest).
bool companion_running(void);

#ifdef __cplusplus
}
#endif
