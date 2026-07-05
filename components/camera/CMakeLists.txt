idf_component_register(
    SRCS "camera.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_video
        esp_driver_ppa
        esp_driver_i2c
        bsp
    PRIV_REQUIRES esp_hw_support
)
