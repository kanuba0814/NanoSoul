#pragma once

typedef enum {
    TASK_EVENT_NONE = 0,
    TASK_EVENT_RULE_MATCHED,
    TASK_EVENT_MODE_CHANGE_REQUESTED,
} task_core_event_id_t;

