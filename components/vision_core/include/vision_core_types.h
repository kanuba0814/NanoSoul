#pragma once

typedef enum {
    VISION_PRESENCE_ABSENT = 0,
    VISION_PRESENCE_PRESENT,
    VISION_PRESENCE_RETURNING,
    VISION_PRESENCE_LEAVING,
} vision_presence_state_t;

typedef struct {
    vision_presence_state_t presence;
    int center_offset_x;
    int center_offset_y;
} vision_presence_snapshot_t;

