#include "app_startup.h"

#include "esp_check.h"
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

esp_err_t app_startup_init(void)
{
    /* Frozen startup order: infrastructure first, orchestration last. */
    ESP_ERROR_CHECK(log_core_init());
    ESP_ERROR_CHECK(bsp_board_init());
    ESP_ERROR_CHECK(diag_core_init());
    ESP_ERROR_CHECK(storage_core_init());
    ESP_ERROR_CHECK(app_core_init());
    ESP_ERROR_CHECK(input_core_init());
    ESP_ERROR_CHECK(ui_core_init());
    ESP_ERROR_CHECK(soul_core_init());
    ESP_ERROR_CHECK(sense_core_init());
    ESP_ERROR_CHECK(speech_core_init());
    ESP_ERROR_CHECK(vision_core_init());
    ESP_ERROR_CHECK(task_core_init());
    ESP_ERROR_CHECK(net_core_init());
    ESP_ERROR_CHECK(audio_core_init());
#if CONFIG_P4_SOULDESK_ENABLE_MOTION_CORE
    ESP_ERROR_CHECK(motion_core_init());
#endif

    return ESP_OK;
}

void app_startup_run(void)
{
    app_core_start_loop();
}
