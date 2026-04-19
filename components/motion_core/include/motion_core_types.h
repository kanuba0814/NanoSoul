#pragma once

#include <stdbool.h>

typedef struct {
    int yaw_degrees;
    int left_wheel_percent;
    int right_wheel_percent;
    bool enabled;
} motion_state_t;

