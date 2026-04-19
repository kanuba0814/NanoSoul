#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "task_core_types.h"

esp_err_t task_core_init(void);
bool task_core_validate_rule(const task_rule_t *rule);

