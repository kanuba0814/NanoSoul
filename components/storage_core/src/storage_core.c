#include "storage_core.h"

#include "bsp_board.h"
#include "sd_storage.h"
#include "storage_core_config.h"

#include "sdkconfig.h"

static esp_err_t s_mount_err = ESP_ERR_INVALID_STATE;
static char **s_wav_paths = NULL;
static size_t s_wav_count = 0;

esp_err_t storage_core_init(void)
{
    s_mount_err = sd_storage_mount(CONFIG_APP_SDCARD_MOUNT_POINT);
    if (s_mount_err != ESP_OK) {
        bsp_board_set_storage_status(HW_STATUS_ERROR);
        return s_mount_err;
    }

    bsp_board_set_storage_status(HW_STATUS_OK);
    if (sd_storage_list_wavs(CONFIG_APP_SDCARD_MOUNT_POINT, &s_wav_paths, &s_wav_count) != ESP_OK) {
        s_wav_paths = NULL;
        s_wav_count = 0;
    }
    return ESP_OK;
}

const char *storage_core_get_config_dir(void)
{
    return STORAGE_CORE_CONFIG_DIR;
}

const char *storage_core_get_log_dir(void)
{
    return STORAGE_CORE_LOG_DIR;
}

const char *storage_core_get_mount_point(void)
{
    return CONFIG_APP_SDCARD_MOUNT_POINT;
}

esp_err_t storage_core_get_mount_error(void)
{
    return s_mount_err;
}

char **storage_core_get_wav_paths(size_t *out_count)
{
    if (out_count) {
        *out_count = s_wav_count;
    }
    return s_wav_paths;
}
