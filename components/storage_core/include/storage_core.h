#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t storage_core_init(void);
const char *storage_core_get_config_dir(void);
const char *storage_core_get_log_dir(void);
const char *storage_core_get_mount_point(void);
esp_err_t storage_core_get_mount_error(void);
char **storage_core_get_wav_paths(size_t *out_count);
