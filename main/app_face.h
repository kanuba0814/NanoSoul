#pragma once
/*
 * Shared FACE-mode bring-up. Both FACE (product) and SELFTEST modes stand up the
 * same runtime (face + HUD, and in later phases camera/vision/soul/netlink/
 * voice); SELFTEST additionally runs the auto-loop diagnostics on top so its
 * checks can probe the live system.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the face runtime. If run_selftest_loop, run the selftest loop after
// (never returns); otherwise return, leaving the runtime tasks running.
void app_face_run(bool run_selftest_loop);

// The motion→TB6612 apply bridge (raw duty, open loop). Exposed so test mode can
// detach it during a motor_test burst (drive a single wheel) and re-attach it.
void app_face_motor_apply(const int16_t duty[3]);

// The bridge motion should currently use: wheel_ctrl_apply when the closed loop
// is up (motion.calib.closed_loop), else the raw-duty bridge above. Test mode
// re-attaches via this so it never bypasses an active closed loop.
typedef void (*app_face_apply_fn)(const int16_t duty[3]);
app_face_apply_fn app_face_motor_bridge(void);

#ifdef __cplusplus
}
#endif
