#pragma once

typedef enum {
    TASK_RULE_TRIGGER = 0,
    TASK_RULE_CONDITION,
    TASK_RULE_ACTION,
} task_rule_node_type_t;

typedef struct {
    const char *trigger;
    const char *condition;
    const char *action;
} task_rule_t;

