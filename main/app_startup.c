#include "app_startup.h"

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "app_core.h"
#include "audio_core.h"
#include "bsp_board.h"
#include "diag_core.h"
#include "input_core.h"
#include "log_core.h"
#include "net_core.h"
#include "sense_core.h"
#include "soul_core.h"
#include "speech_core.h"
#include "storage_core.h"
#include "task_core.h"
#include "ui_core.h"
#include "vision_core.h"
#include "motion_core.h"

static const char *TAG = "app_startup";

static void init_optional(const char *name, esp_err_t (*init_fn)(void))
{
    esp_err_t err = init_fn();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s init failed: %s", name, esp_err_to_name(err));
    }
}

esp_err_t app_startup_init(void)
{
    ESP_ERROR_CHECK(log_core_init());
    ESP_LOGI(TAG, "NanoSoul boot");
    ESP_ERROR_CHECK(bsp_board_init());

    init_optional("storage_core", storage_core_init);
    init_optional("audio_core", audio_core_init);
    init_optional("sense_core", sense_core_init);
    init_optional("net_core", net_core_init);
    init_optional("vision_core", vision_core_init);

    ESP_ERROR_CHECK(diag_core_init());
    ESP_ERROR_CHECK(ui_core_init());

    ESP_ERROR_CHECK(app_core_init());
    ESP_ERROR_CHECK(input_core_init());
    ESP_ERROR_CHECK(soul_core_init());
    ESP_ERROR_CHECK(speech_core_init());
    ESP_ERROR_CHECK(task_core_init());
#if CONFIG_NANOSOUL_ENABLE_MOTION_CORE
    ESP_ERROR_CHECK(motion_core_init());
#endif

    return ESP_OK;
}

void app_startup_run(void)
{
    app_core_start_loop();
}
