#include "soul_core.h"

#include <stddef.h>

#include "soul_core_config.h"

static soul_profile_t s_profile = {
    .warm = 50,
    .proactive = 50,
    .talkative = 50,
    .strict = 50,
};

esp_err_t soul_core_init(void)
{
    return ESP_OK;
}

bool soul_core_validate(const soul_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }

    return profile->warm >= SOUL_CORE_PARAM_MIN &&
           profile->warm <= SOUL_CORE_PARAM_MAX &&
           profile->proactive >= SOUL_CORE_PARAM_MIN &&
           profile->proactive <= SOUL_CORE_PARAM_MAX &&
           profile->talkative >= SOUL_CORE_PARAM_MIN &&
           profile->talkative <= SOUL_CORE_PARAM_MAX &&
           profile->strict >= SOUL_CORE_PARAM_MIN &&
           profile->strict <= SOUL_CORE_PARAM_MAX;
}

soul_profile_view_t soul_core_get_view(void)
{
    soul_profile_view_t view = {
        .persona_style = "balanced",
        .copy_template = "default",
        .reminder_intensity = (s_profile.proactive + s_profile.strict) / 2,
        .valid = soul_core_validate(&s_profile),
    };

    return view;
}
