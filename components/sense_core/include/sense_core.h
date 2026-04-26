#pragma once

#include "esp_err.h"
#include "sense_core_types.h"

esp_err_t sense_core_init(void);
sense_snapshot_t sense_core_get_snapshot(void);
esp_err_t sense_get_tof_array(tof_array_state_t *out);
