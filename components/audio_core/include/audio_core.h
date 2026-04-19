#pragma once

#include "audio_core_types.h"
#include "esp_err.h"

esp_err_t audio_core_init(void);
audio_volume_profile_t audio_core_get_profile(void);

