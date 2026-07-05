#include "sd_storage.h"
#include "bsp_pins.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

static sd_pwr_ctrl_handle_t s_pwr_ctrl = NULL;
static sdmmc_card_t        *s_card = NULL;
static bool                 s_mounted = false;
static char                 s_mount_point[24];

/* Enable the on-chip LDO_VO4 rail that feeds the SD IO pins (required on this
 * board — without it the card never responds). */
static esp_err_t ensure_sd_io_ldo(sdmmc_host_t *host)
{
    if (s_pwr_ctrl == NULL) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = BSP_SD_LDO_CHAN,
        };
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl),
                            TAG, "enable SD IO LDO");
        ESP_LOGI(TAG, "Enabled SD IO LDO via LDO_VO%d", BSP_SD_LDO_CHAN);
    }
    host->pwr_ctrl_handle = s_pwr_ctrl;
    return ESP_OK;
}

static void configure_spi_pullups(void)
{
    gpio_set_pull_mode(BSP_SD_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SD_CMD, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SD_D0, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SD_D1, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SD_D2, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BSP_SD_D3, GPIO_PULLUP_ONLY);
}

static esp_err_t mount_sdspi(const char *mount_point, int max_freq_khz)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = max_freq_khz;
    ESP_RETURN_ON_ERROR(ensure_sd_io_ldo(&host), TAG, "sd io ldo");

    configure_spi_pullups();

    spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_SD_CMD,
        .miso_io_num = BSP_SD_D0,
        .sclk_io_num = BSP_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };

    esp_err_t err = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_SD_D3;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting SDSPI %s @ %d kHz (CLK=%d MOSI=%d MISO=%d CS=%d)",
             mount_point, max_freq_khz, BSP_SD_CLK, BSP_SD_CMD, BSP_SD_D0, BSP_SD_D3);

    err = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI mount failed: %s", esp_err_to_name(err));
        spi_bus_free(host.slot);
        s_card = NULL;
        return err;
    }

    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sd_storage_mount(const char *mount_point)
{
    if (s_mounted) {
        return ESP_OK;
    }
    esp_err_t err = mount_sdspi(mount_point, SDMMC_FREQ_DEFAULT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Retrying SD mount at 10 MHz");
        err = mount_sdspi(mount_point, 10000);
    }
    if (err == ESP_OK) {
        s_mounted = true;
        strlcpy(s_mount_point, mount_point, sizeof(s_mount_point));
    }
    return err;
}

bool sd_storage_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_storage_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_mounted) {
        if (total_bytes) *total_bytes = 0;
        if (free_bytes) *free_bytes = 0;
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t total = 0, freeb = 0;
    esp_err_t err = esp_vfs_fat_info(s_mount_point, &total, &freeb);
    if (err == ESP_OK) {
        if (total_bytes) *total_bytes = total;
        if (free_bytes) *free_bytes = freeb;
    }
    return err;
}
