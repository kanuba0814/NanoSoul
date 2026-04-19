#pragma once

#include <stdbool.h>

typedef enum {
    SENSE_DISTANCE_UNKNOWN = 0,
    SENSE_DISTANCE_FAR,
    SENSE_DISTANCE_NEAR,
} sense_distance_state_t;

typedef enum {
    SENSE_LIGHT_UNKNOWN = 0,
    SENSE_LIGHT_DAY,
    SENSE_LIGHT_NIGHT,
} sense_light_state_t;

typedef enum {
    SENSE_NOISE_UNKNOWN = 0,
    SENSE_NOISE_QUIET,
    SENSE_NOISE_BUSY,
} sense_noise_state_t;

typedef struct {
    sense_distance_state_t distance;
    sense_light_state_t light;
    sense_noise_state_t noise;
    bool user_near;
} sense_snapshot_t;

