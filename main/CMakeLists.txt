idf_component_register(
    SRCS
        "nanosoul_main.c"
        "app_selftests.c"
        "app_face.c"
        "drv_motor.c"
        "drv_encoder.c"
        "drv_ina219.c"
        "test_app.c"
    INCLUDE_DIRS "."
    REQUIRES
        # always-on core
        telemetry
        storage_sd
        selftest
        nvs_flash
        esp_partition
        # FACE runtime
        face
        hud
        board_i2c0
        camera
        vision
        motion
        soul
        netlink
        llm
        dialog
        # TESTPANEL / legacy motor test path
        display
        esp_driver_ledc
        esp_driver_gpio
        esp_driver_pcnt
        esp_driver_i2c
        esp_timer
        lvgl
)
