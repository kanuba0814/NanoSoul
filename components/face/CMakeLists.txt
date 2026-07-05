idf_component_register(
    SRCS "face.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_emote_gen_player   # gfx runtime + emote player (public: gfx.h)
        display                # panel_st7701.h
        esp_lcd
    PRIV_REQUIRES
        esp_driver_gpio
        esp_hw_support
)
