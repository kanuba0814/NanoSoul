idf_component_register(
    SRCS
        "sd_storage.c"
        "ns_config.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp
    PRIV_REQUIRES
        fatfs
        sdmmc
        esp_driver_sdspi
        esp_driver_sdmmc
        esp_driver_spi
        esp_driver_gpio
        json
)
