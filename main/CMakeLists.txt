idf_component_register(
    SRCS
        "nanosoul_main.c"
        "drv_motor.c"
        "drv_encoder.c"
        "drv_ina219.c"
        "test_app.c"
    INCLUDE_DIRS "."
    REQUIRES
        display
        esp_driver_ledc
        esp_driver_gpio
        esp_driver_pcnt
        esp_driver_i2c
        esp_timer
        lvgl
)
