#include "motion_core.h"

#include <stddef.h>

static motion_state_t s_motion_state = MOTION_STATE_DISABLED;

esp_err_t motion_core_init(void)
{
    s_motion_state = MOTION_STATE_DISABLED;
    return ESP_OK;
}

motion_state_t motion_core_get_state(void)
{
    return s_motion_state;
}

esp_err_t motion_core_request(const motion_request_t *request)
{
    if (request == NULL || request->type == MOTION_REQUEST_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_motion_state == MOTION_STATE_DISABLED) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_ERR_NOT_SUPPORTED;
}
