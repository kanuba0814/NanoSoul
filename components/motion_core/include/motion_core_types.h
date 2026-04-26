#pragma once

#include <stdint.h>

typedef enum {
    MOTION_STATE_DISABLED = 0,
    MOTION_STATE_LOCKED,
    MOTION_STATE_READY,
    MOTION_STATE_MOVING,
    MOTION_STATE_FAULT,
} motion_state_t;

typedef enum {
    MOTION_REQUEST_NONE = 0,
    MOTION_REQUEST_LOCK,
    MOTION_REQUEST_UNLOCK,
    MOTION_REQUEST_STOP,
    MOTION_REQUEST_MOVE_RELATIVE,
} motion_request_type_t;

typedef struct {
    motion_request_type_t type;
    int direction_degrees;
    int distance_mm;
    uint32_t duration_ms;
} motion_request_t;
