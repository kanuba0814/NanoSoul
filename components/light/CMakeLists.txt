idf_component_register(
    SRCS "light.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c telemetry storage_sd
    PRIV_REQUIRES esp_timer simsense
)
