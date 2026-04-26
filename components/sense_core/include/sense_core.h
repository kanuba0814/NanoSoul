#pragma once

#include "esp_err.h"
#include "drivers/bh1750.h"
#include "sense_core_types.h"

esp_err_t sense_core_init(void);
sense_snapshot_t sense_core_get_snapshot(void);
esp_err_t sense_get_tof_array(tof_array_state_t *out);
hw_status_t sense_core_get_bh1750_status(void);
esp_err_t sense_core_read_bh1750(bh1750_sample_t *sample);
