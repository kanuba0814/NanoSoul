#include "task_core.h"

#include <stddef.h>

esp_err_t task_core_init(void)
{
    return ESP_OK;
}

bool task_core_validate_rule(const task_rule_t *rule)
{
    if (rule == NULL) {
        return false;
    }

    return rule->trigger != NULL && rule->condition != NULL && rule->action != NULL;
}
