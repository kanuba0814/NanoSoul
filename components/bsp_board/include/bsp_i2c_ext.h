#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_i2c_ext_init(void);
esp_err_t bsp_i2c_ext_lock(TickType_t timeout);
void bsp_i2c_ext_unlock(void);
esp_err_t bsp_i2c_ext_write(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                              TickType_t timeout);
esp_err_t bsp_i2c_ext_read(uint8_t addr, uint8_t *read_buf, size_t read_len,
                             TickType_t timeout);
esp_err_t bsp_i2c_ext_write_read(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                                   uint8_t *read_buf, size_t read_len, TickType_t timeout);
i2c_port_t bsp_i2c_ext_port(void);
i2c_master_bus_handle_t bsp_i2c_ext_get_handle(void);

/* Probe a 7-bit address by issuing START + write-address + STOP.
 * Sets *present to true if a slave ACKed; returns ESP_OK on success.
 * Returns lock-related errors if the bus is unavailable. */
esp_err_t bsp_i2c_ext_probe(uint8_t addr, bool *present, TickType_t timeout);

#ifdef __cplusplus
}
#endif
