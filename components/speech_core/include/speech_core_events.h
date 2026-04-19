#pragma once

typedef enum {
    SPEECH_EVENT_NONE = 0,
    SPEECH_EVENT_WAKE_WORD_DETECTED,
    SPEECH_EVENT_COMMAND_DETECTED,
} speech_core_event_id_t;

