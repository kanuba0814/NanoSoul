#pragma once

#include "esp_err.h"
#include "vision_core_types.h"

esp_err_t vision_core_init(void);
vision_presence_snapshot_t vision_core_get_snapshot(void);

