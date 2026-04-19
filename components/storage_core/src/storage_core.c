#include "storage_core.h"

#include "storage_core_config.h"

esp_err_t storage_core_init(void)
{
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

