idf_component_register(
    SRCS "telemetry.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
    PRIV_REQUIRES esp_timer
)
