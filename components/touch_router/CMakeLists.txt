idf_component_register(
    SRCS "touch_router.c"
    INCLUDE_DIRS "include"
    REQUIRES touch telemetry esp_driver_i2c
    PRIV_REQUIRES esp_timer
)
