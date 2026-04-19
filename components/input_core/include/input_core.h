#pragma once

#include "esp_err.h"
#include "input_core_events.h"

esp_err_t input_core_init(void);
input_core_event_id_t input_core_get_last_event(void);

