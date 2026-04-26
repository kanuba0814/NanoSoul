#pragma once

#include "world_state_types.h"

typedef enum {
    TASK_RULE_TRIGGER = 0,
    TASK_RULE_CONDITION,
    TASK_RULE_ACTION,
} task_rule_node_type_t;

typedef enum {
    TASK_TRIGGER_VOICE_COMMAND = 0,
    TASK_TRIGGER_PRESENCE_CHANGE,
    TASK_TRIGGER_LIGHT_CHANGE,
    TASK_TRIGGER_MOTION_STATE_CHANGE,
    TASK_TRIGGER_TIME_TICK,
    TASK_TRIGGER_SYSTEM_EVENT,
} task_trigger_type_t;

typedef enum {
    TASK_ACTION_SET_APP_MODE = 0,
    TASK_ACTION_UPDATE_UI,
    TASK_ACTION_PLAY_PROMPT,
    TASK_ACTION_SET_PERSONA,
    TASK_ACTION_SET_REMINDER,
    TASK_ACTION_REQUEST_MOTION,
} task_action_type_t;

typedef struct {
    const char *trigger;
    const char *condition;
    const char *action;
} task_rule_t;

typedef struct {
    const world_state_t *world;
} task_eval_context_t;
