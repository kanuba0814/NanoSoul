#include "drv_ina219.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ina219";

#define I2C_FREQ_HZ     100000        // 长线/多挂兼容（docs：100kHz + 2.2k 上拉）
#define INA219_ADDR     0x40
#define REG_SHUNT       0x01          // 分流电压寄存器，LSB = 10µV（有符号）
#define SHUNT_OHM       0.1f          // 模块板载 0.1Ω 采样电阻
#define SHUNT_LSB_V     10e-6f

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_present = false;

esp_err_t ina219_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        ESP_LOGW(TAG, "no I2C1 bus → 跳过电流显示");
        return ESP_OK;
    }
    if (i2c_master_probe(bus, INA219_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "INA219 @0x%02X 未探到 → 跳过电流显示", INA219_ADDR);
        return ESP_OK;   // 没接也算成功，仅置 absent
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA219_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add dev");
    s_present = true;
    ESP_LOGI(TAG, "INA219 在线 @0x%02X (shared I2C1)", INA219_ADDR);
    return ESP_OK;
}

bool ina219_present(void)
{
    return s_present;
}

float ina219_current_a(void)
{
    if (!s_present || !s_dev) {
        return 0.0f;
    }
    uint8_t reg = REG_SHUNT;
    uint8_t rx[2] = { 0 };
    if (i2c_master_transmit_receive(s_dev, &reg, 1, rx, 2, 100) != ESP_OK) {
        return 0.0f;
    }
    int16_t raw = (int16_t)((rx[0] << 8) | rx[1]);   // 大端、有符号
    float vshunt = raw * SHUNT_LSB_V;
    return vshunt / SHUNT_OHM;
}
