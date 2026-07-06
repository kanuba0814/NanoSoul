idf_component_register(
    SRCS "motion.c"
    INCLUDE_DIRS "include"
    REQUIRES telemetry
    PRIV_REQUIRES esp_timer
)
