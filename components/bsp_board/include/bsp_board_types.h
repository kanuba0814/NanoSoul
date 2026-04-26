#pragma once

typedef enum {
    HW_STATUS_ABSENT = 0,
    HW_STATUS_OK,
    HW_STATUS_STALE,
    HW_STATUS_ERROR,
    HW_STATUS_DISABLED,

    /* Compatibility aliases for earlier NanoSoul docs/types. */
    HW_STATUS_UNKNOWN = HW_STATUS_ABSENT,
    HW_STATUS_FAULT = HW_STATUS_ERROR,
} hw_status_t;

typedef struct {
    hw_status_t display;
    hw_status_t touch;
    hw_status_t audio;
    hw_status_t storage;
    hw_status_t camera;
    hw_status_t wifi;

    hw_status_t i2c_board;
    hw_status_t i2c_ext;

    hw_status_t bh1750;
    hw_status_t vl6180x_l;
    hw_status_t vl6180x_c;
    hw_status_t vl6180x_r;

    hw_status_t motion;
} bsp_board_status_t;
