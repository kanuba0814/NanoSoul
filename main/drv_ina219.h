// INA219 电机电流（无 PCB 版：I²C1 SDA=IO20/SCL=IO21, 0x40）。
// 自动检测：探到才读，没探到标 absent，不影响其余测试。
#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// 挂到共享 I²C1 总线 + 探测 0x40。探不到也返回 ESP_OK（present 置 false）。
// bus 由 board_i2c1 统一创建（与 IMU/BH1750 共总线）。
esp_err_t ina219_init(i2c_master_bus_handle_t bus);

// 0x40 是否在线。
bool ina219_present(void);

// 电机电流（A）。不在线返回 0。读分流电压寄存器算：I = Vshunt / 0.1Ω。
float ina219_current_a(void);
