#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "soul_core_types.h"

esp_err_t soul_core_init(void);
bool soul_core_validate(const soul_profile_t *profile);
soul_profile_view_t soul_core_get_view(void);

