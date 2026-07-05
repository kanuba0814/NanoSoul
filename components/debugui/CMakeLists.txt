idf_component_register(
    SRCS "debugui.c"
    INCLUDE_DIRS "include"
    REQUIRES touch face camera telemetry esp_driver_i2c esp_timer lvgl
)
