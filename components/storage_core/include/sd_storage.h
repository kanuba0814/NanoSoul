#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_storage_mount(const char *mount_point);

/*
 * Scan `dir` (non-recursive) for files ending with ".wav" (case-insensitive).
 * Caller must free via sd_storage_free_list().
 */
esp_err_t sd_storage_list_wavs(const char *dir, char ***out_paths, size_t *out_count);

void sd_storage_free_list(char **paths, size_t count);

#ifdef __cplusplus
}
#endif
