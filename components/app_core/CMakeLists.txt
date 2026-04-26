idf_component_register(
    SRCS "src/app_core.c"
    INCLUDE_DIRS "include"
    REQUIRES
        motion_core
        net_core
        sense_core
        speech_core
        vision_core
)
