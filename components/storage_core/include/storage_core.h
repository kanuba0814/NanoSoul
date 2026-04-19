#pragma once

#include "esp_err.h"

esp_err_t storage_core_init(void);
const char *storage_core_get_config_dir(void);
const char *storage_core_get_log_dir(void);

