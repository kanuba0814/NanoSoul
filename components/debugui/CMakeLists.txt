idf_component_register(
    SRCS "debugui.c"
    INCLUDE_DIRS "include"
    REQUIRES touch touch_router face camera telemetry netlink esp_driver_i2c esp_timer lvgl
)
