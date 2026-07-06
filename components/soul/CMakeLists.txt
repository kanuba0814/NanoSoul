idf_component_register(
    SRCS "soul.c" "soul_expr.c"
    INCLUDE_DIRS "include"
    REQUIRES
        telemetry
        storage_sd   # ns_config
        vision
        motion
        face
        esp_timer
)
