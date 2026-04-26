#include "sd_storage.h"
#include "bsp_board_pins.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"

static const char *TAG = "sd";
static sd_pwr_ctrl_handle_t s_pwr_ctrl = NULL;

static esp_err_t ensure_sd_io_ldo(sdmmc_host_t *host)
{
    if (s_pwr_ctrl == NULL) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = 4,
        };
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl),
                            TAG, "enable SD IO LDO");
        ESP_LOGI(TAG, "Enabled on-chip SD IO LDO via LDO_VO4");
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

    sdmmc_card_t *card = NULL;
    ESP_LOGI(TAG, "Mounting SDSPI %s (max %d kHz)", mount_point, max_freq_khz);
    ESP_LOGI(TAG, "SD SPI pins CLK=%d MOSI/CMD=%d MISO/D0=%d CS/D3=%d (D1=%d D2=%d pulled up)",
             BSP_SD_CLK, BSP_SD_CMD, BSP_SD_D0, BSP_SD_D3, BSP_SD_D1, BSP_SD_D2);
    err = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config,
                                  &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI mount failed: %s", esp_err_to_name(err));
        spi_bus_free(host.slot);
        return err;
    }

    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

esp_err_t sd_storage_mount(const char *mount_point)
{
    esp_err_t err = mount_sdspi(mount_point, SDMMC_FREQ_DEFAULT);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Retrying SD mount in SPI mode at 10 MHz");
    return mount_sdspi(mount_point, 10000);
}

static bool ends_with_wav(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && strcasecmp(name + n - 4, ".wav") == 0;
}

static int cmp_strp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

esp_err_t sd_storage_list_wavs(const char *dir, char ***out_paths, size_t *out_count)
{
    *out_paths = NULL;
    *out_count = 0;

    DIR *d = opendir(dir);
    if (!d) {
        ESP_LOGE(TAG, "opendir(%s) failed", dir);
        return ESP_FAIL;
    }

    size_t cap = 8;
    size_t n = 0;
    char **list = calloc(cap, sizeof(char *));
    if (!list) { closedir(d); return ESP_ERR_NO_MEM; }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!ends_with_wav(e->d_name)) continue;

        size_t plen = strlen(dir) + 1 + strlen(e->d_name) + 1;
        char *full = malloc(plen);
        if (!full) break;
        snprintf(full, plen, "%s/%s", dir, e->d_name);

        if (n == cap) {
            cap *= 2;
            char **grown = realloc(list, cap * sizeof(char *));
            if (!grown) { free(full); break; }
            list = grown;
        }
        list[n++] = full;
    }
    closedir(d);

    if (n > 1) qsort(list, n, sizeof(char *), cmp_strp);

    ESP_LOGI(TAG, "Found %u wav file(s) in %s", (unsigned)n, dir);
    *out_paths = list;
    *out_count = n;
    return ESP_OK;
}

void sd_storage_free_list(char **paths, size_t count)
{
    if (!paths) return;
    for (size_t i = 0; i < count; ++i) free(paths[i]);
    free(paths);
}
