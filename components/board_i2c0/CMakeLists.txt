idf_component_register(
    SRCS "board_i2c0.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c bsp
)
