#pragma once

#include <stdint.h>

#include "app_core_types.h"
#include "motion_core_types.h"
#include "net_core_types.h"
#include "sense_core_types.h"
#include "speech_core_types.h"
#include "vision_core_types.h"

typedef struct {
    app_mode_t app_mode;
    sense_snapshot_t sense;
    vision_presence_snapshot_t vision;
    speech_command_id_t last_speech_command;
    net_wifi_state_t wifi;
    net_cloud_state_t cloud;
    motion_state_t motion;
    uint32_t timestamp_ms;
} world_state_t;

