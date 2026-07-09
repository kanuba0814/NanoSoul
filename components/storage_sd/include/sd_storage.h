#pragma once
/*
 * SD card storage — mounts the board TF slot in SDSPI mode (4-bit SDMMC is
 * avoided; SDSPI is what /home/gxxl/testP4 verified on this exact board). The
 * on-chip LDO_VO4 rail that powers the SD IO must be enabled first.
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_MOUNT_POINT "/sdcard"

esp_err_t sd_storage_mount(const char *mount_point);
bool      sd_storage_mounted(void);
/* Total / free bytes of the mounted FAT volume; 0 if unmounted. */
esp_err_t sd_storage_usage(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
