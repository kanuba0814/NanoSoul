#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_lock(TickType_t timeout);
void bsp_i2c_unlock(void);
esp_err_t bsp_i2c_write(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                          TickType_t timeout);
esp_err_t bsp_i2c_write_read(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                               uint8_t *read_buf, size_t read_len, TickType_t timeout);
i2c_port_t bsp_i2c_port(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

#ifdef __cplusplus
}
#endif
