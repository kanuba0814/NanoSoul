idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_i2c esp_codec_dev bsp esp_timer
)
