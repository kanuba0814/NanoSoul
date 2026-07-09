#pragma once
/*
 * board_i2c0 — the on-board I2C0 master bus (GPIO7/8).
 *
 * Shared by the OV5647 SCCB (camera), the ES8311 codec, and the FT6x36 touch
 * controller. Created once here so those subsystems attach to the same bus
 * instead of fighting over the pins.
 */

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t                board_i2c0_init(void);
i2c_master_bus_handle_t  board_i2c0_bus(void);

// True if a device ACKs at `addr` (for selftest probes).
bool board_i2c0_probe(uint8_t addr);

#ifdef __cplusplus
}
#endif
