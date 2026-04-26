#include "diag_core.h"

#include "bsp_board.h"

static diag_health_state_t s_health = DIAG_HEALTH_OK;

esp_err_t diag_core_init(void)
{
    const bsp_board_status_t *status = bsp_board_get_status();
    if (status->display == HW_STATUS_ERROR) {
        s_health = DIAG_HEALTH_ERROR;
    } else if (status->touch == HW_STATUS_ERROR ||
               status->audio == HW_STATUS_ERROR ||
               status->storage == HW_STATUS_ERROR ||
               status->camera == HW_STATUS_ERROR ||
               status->wifi == HW_STATUS_ERROR ||
               status->bh1750 == HW_STATUS_ERROR) {
        s_health = DIAG_HEALTH_WARN;
    } else {
        s_health = DIAG_HEALTH_OK;
    }
    return ESP_OK;
}

diag_health_state_t diag_core_get_health(void)
{
    return s_health;
}

const bsp_board_status_t *diag_core_get_board_status(void)
{
    return bsp_board_get_status();
}

const char *diag_core_hw_status_name(hw_status_t status)
{
    return bsp_board_hw_status_name(status);
}
