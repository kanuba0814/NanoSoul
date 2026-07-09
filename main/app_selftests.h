#pragma once
/*
 * Central registration point for all selftest checks. main calls this after the
 * subsystems it wants tested are up. Each phase adds its checks here, guarded so
 * absent hardware SKIPs rather than fails.
 */
#ifdef __cplusplus
extern "C" {
#endif

void app_selftests_register(void);

#ifdef __cplusplus
}
#endif
