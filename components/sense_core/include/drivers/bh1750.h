#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t addr;
    uint16_t raw;
    float lux;
} bh1750_sample_t;

esp_err_t bh1750_init(void);
esp_err_t bh1750_read(bh1750_sample_t *sample);
uint8_t bh1750_addr(void);

#ifdef __cplusplus
}
#endif
