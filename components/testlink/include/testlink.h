#pragma once
/*
 * testlink — transport-agnostic protocol core shared by the companion WS server
 * and the USB-Serial-JTAG test link (docs/13).
 *
 * Owns: command dispatch (ns_proto_handle), the 1 Hz `state` heartbeat, the
 * telemetry event bus → `event` frame forwarding, and a small sink registry so
 * every transport that registers gets the same state/event/sense frames. The
 * companion component is now just the WS transport shell; the serial link (T4)
 * is a second transport over the same core.
 *
 * All new frames/commands are additive per docs/09 前向兼容铁律: unknown `type`
 * is ignored by old peers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- frame sinks: transports register to receive state/event/sense frames ---- */
typedef void (*ns_sink_fn)(const char *json);
esp_err_t ns_link_add_sink(ns_sink_fn fn);
void      ns_link_broadcast(const char *json);   /* fan a text frame to every sink */

/* ---- command dispatch (transport-agnostic) ---- */
/* reply writes one text frame back on the same transport the command arrived on. */
typedef void (*ns_reply_fn)(void *ctx, const char *json);
void ns_proto_handle(const char *json, size_t len, ns_reply_fn reply, void *ctx);

/* get_snapshot is WS-binary only; a transport that can send it registers a hook.
 * Return true if a snapshot frame was actually sent. */
typedef bool (*ns_snapshot_fn)(void *ctx);
void ns_proto_set_snapshot_hook(ns_snapshot_fn fn);

/* motor_test/motor_stop drive the H-bridges; drv_motor lives in main/, so main
 * registers these hooks. motor_test is gated to test mode (default off). */
typedef struct {
    bool (*test_run)(int m, int duty, int ms);  /* true=accepted; false=busy */
    void (*stop_all)(void);
} ns_motor_hooks_t;
void ns_proto_set_motor_hooks(const ns_motor_hooks_t *h);
void ns_proto_set_test_mode(bool on);

/* ---- shared core: esp_event → event frames + 1 Hz state push. Idempotent. ---- */
esp_err_t testlink_core_start(void);
char     *ns_build_state_json(void);   /* heap string; caller frees */

/* ---- sense stream (T3): high-rate raw sensor frames, default off ---- */
void ns_sense_set_rate(int hz);        /* 0=off, clamped to [0,50] */
int  ns_sense_get_rate(void);
char *ns_build_hwinfo_json(const char *mode);  /* heap string; caller frees */

/* ---- serial link (T4): USB-Serial-JTAG NDJSON transport (test mode) ---- */
esp_err_t testlink_serial_start(void);
bool      testlink_serial_running(void);

#ifdef __cplusplus
}
#endif
