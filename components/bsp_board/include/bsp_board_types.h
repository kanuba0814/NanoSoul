#pragma once

#include <stdbool.h>

typedef struct {
    bool display_ready;
    bool touch_ready;
    bool camera_ready;
    bool audio_ready;
    bool storage_ready;
    bool wireless_ready;
} bsp_board_status_t;

