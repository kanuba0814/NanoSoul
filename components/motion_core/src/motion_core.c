#include "motion_core.h"

static motion_state_t s_motion_state = {
    .yaw_degrees = 0,
    .left_wheel_percent = 0,
    .right_wheel_percent = 0,
    .enabled = false,
};

esp_err_t motion_core_init(void)
{
    return ESP_OK;
}

motion_state_t motion_core_get_state(void)
{
    return s_motion_state;
}

