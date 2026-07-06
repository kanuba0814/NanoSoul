idf_component_register(
    SRCS "imu.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c telemetry motion storage_sd
    PRIV_REQUIRES esp_timer simsense
)
