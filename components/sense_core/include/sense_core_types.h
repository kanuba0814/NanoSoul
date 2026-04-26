#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_board_types.h"

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
    hw_status_t status;
    uint16_t range_mm;
    uint32_t timestamp_ms;
} tof_sensor_state_t;

typedef struct {
    tof_sensor_state_t tof_l;
    tof_sensor_state_t tof_c;
    tof_sensor_state_t tof_r;
} tof_array_state_t;

typedef struct {
    sense_distance_state_t distance;
    sense_light_state_t light;
    sense_noise_state_t noise;
    tof_array_state_t tof;
    bool user_near;
} sense_snapshot_t;
