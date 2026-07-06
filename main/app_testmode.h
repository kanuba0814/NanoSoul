#pragma once
/*
 * TEST mode (docs/13). Entered when IO48 is strapped to GND at boot (or the
 * NANOSOUL_MODE_TEST Kconfig override for board-off builds). Stands up the full
 * FACE runtime — soul keeps running, so injected sensor values drive the real
 * decision code — and adds the test services: motor_test hooks, the USB-Serial-
 * JTAG NDJSON link, a forced connection-info HUD, and a long-press E-stop.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_test_run(void);

#ifdef __cplusplus
}
#endif
