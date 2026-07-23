#pragma once
/*
 * light — BH1750 ambient light sensor on board I2C1 (addr 0x23). Polls at 1 Hz
 * and posts NS_EVT_DARK / NS_EVT_BRIGHT with hysteresis (docs/12 S10). Bare
 * register driver — no external component, to avoid new/old i2c driver clashes.
 */

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe + configure on the shared bus; start the 1 Hz poll task. Absent sensor
 * -> ESP_OK, present() stays false (graceful degrade). */
esp_err_t light_init(i2c_master_bus_handle_t bus);
bool      light_present(void);
float     light_lux(void);   /* last reading, 0 if absent */

#ifdef __cplusplus
}
#endif
