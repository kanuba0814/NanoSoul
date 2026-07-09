idf_component_register(
    SRCS "hud.c"
    INCLUDE_DIRS "include"
    REQUIRES
        face        # gfx display + frame count
        telemetry
        vision      # detect fps readout
        lvgl        # lv_font_t for the shared PuHui font
    PRIV_REQUIRES esp_timer
)
