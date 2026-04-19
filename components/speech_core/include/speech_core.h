#pragma once

#include "esp_err.h"
#include "speech_core_types.h"

esp_err_t speech_core_init(void);
speech_command_id_t speech_core_get_last_command(void);

