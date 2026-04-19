#pragma once

#include <stdbool.h>

typedef struct {
    int warm;
    int proactive;
    int talkative;
    int strict;
} soul_profile_t;

typedef struct {
    const char *persona_style;
    const char *copy_template;
    int reminder_intensity;
    bool valid;
} soul_profile_view_t;

