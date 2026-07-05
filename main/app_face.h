#pragma once
/*
 * Shared FACE-mode bring-up. Both FACE (product) and SELFTEST modes stand up the
 * same runtime (face + HUD, and in later phases camera/vision/soul/netlink/
 * voice); SELFTEST additionally runs the auto-loop diagnostics on top so its
 * checks can probe the live system.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the face runtime. If run_selftest_loop, run the selftest loop after
// (never returns); otherwise return, leaving the runtime tasks running.
void app_face_run(bool run_selftest_loop);

#ifdef __cplusplus
}
#endif
