#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PRESENCE_STATE_UNKNOWN = 0,
    PRESENCE_STATE_ABSENT,
    PRESENCE_STATE_PRESENT,
} presence_state_t;

typedef struct {
    presence_state_t presence;
    bool user_present;
    int x_offset;
    uint8_t confidence;
    uint32_t timestamp_ms;
} vision_presence_snapshot_t;
