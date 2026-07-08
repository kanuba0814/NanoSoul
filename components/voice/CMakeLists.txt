idf_component_register(
    SRCS "voice.c"
    INCLUDE_DIRS "include"
    REQUIRES audio llm soul face telemetry netlink storage_sd esp_driver_i2c
)
