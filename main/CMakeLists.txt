idf_component_register(
    SRCS
        "nanosoul_main.c"
        "test_app.c"
        "drv_motor.c"
        "drv_encoder.c"
        "drv_ina219.c"
    INCLUDE_DIRS "."
    REQUIRES
        # 屏 + 触摸 + I²C 总线
        display
        board_i2c0
        board_i2c1
        lvgl
        # 相机 (OV5647 MIPI-CSI via esp_video)
        camera
        # 电机/编码器/电流驱动所需的 IDF 外设
        esp_driver_ledc
        esp_driver_gpio
        esp_driver_pcnt
        esp_driver_i2c
        esp_timer
)
