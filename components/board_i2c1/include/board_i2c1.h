#pragma once
/*
 * board_i2c1 — the off-board sensor I2C bus (SDA=IO20, SCL=IO21, 100 kHz),
 * shared by the external sensor modules of the no-PCB build: INA219 (motor
 * current), QMI8658 (IMU), BH1750 (ambient light). One master bus created once;
 * every driver adds its own device handle onto board_i2c1_bus(). See CLAUDE.md
 * firmware section.
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t                board_i2c1_init(void);   /* idempotent */
i2c_master_bus_handle_t  board_i2c1_bus(void);    /* NULL if init failed */
bool                     board_i2c1_probe(uint8_t addr);

#ifdef __cplusplus
}
#endif
