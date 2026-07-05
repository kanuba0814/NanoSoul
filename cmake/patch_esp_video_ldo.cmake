# Keep esp_video's CSI DPHY LDO aligned with the display's MIPI rail.
#
# On this board the DSI display and the CSI camera share the MIPI DPHY power
# rail (on-chip LDO_VO3 @ 2.5 V). esp_video defaults its CSI LDO to a different
# unit/voltage, so the camera never powers up unless we point it at VO3/2500mV.
# Verified against /home/gxxl/testP4 (commit "Fix camera CSI DPHY LDO rail").
#
# Runs at configure time, after project() has fetched managed_components.
get_filename_component(_project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_esp_video_csi_device
    "${_project_root}/managed_components/espressif__esp_video/src/device/esp_video_csi_device.c")

if(EXISTS "${_esp_video_csi_device}")
    file(READ "${_esp_video_csi_device}" _esp_video_csi_source)

    set(_esp_video_csi_patched FALSE)
    if(_esp_video_csi_source MATCHES "#define CSI_LDO_UNIT_ID[ \t]+[0-9]+")
        string(REGEX REPLACE "#define CSI_LDO_UNIT_ID[ \t]+[0-9]+"
                             "#define CSI_LDO_UNIT_ID             3"
                             _esp_video_csi_source "${_esp_video_csi_source}")
        set(_esp_video_csi_patched TRUE)
    else()
        message(FATAL_ERROR "Unable to locate esp_video CSI_LDO_UNIT_ID definition")
    endif()

    if(_esp_video_csi_source MATCHES "#define CSI_LDO_CFG_VOL_MV[ \t]+[0-9]+")
        string(REGEX REPLACE "#define CSI_LDO_CFG_VOL_MV[ \t]+[0-9]+"
                             "#define CSI_LDO_CFG_VOL_MV          2500"
                             _esp_video_csi_source "${_esp_video_csi_source}")
        set(_esp_video_csi_patched TRUE)
    else()
        message(FATAL_ERROR "Unable to locate esp_video CSI_LDO_CFG_VOL_MV definition")
    endif()

    if(_esp_video_csi_patched)
        file(WRITE "${_esp_video_csi_device}" "${_esp_video_csi_source}")
        message(STATUS "NanoSoul: esp_video CSI LDO pinned to LDO_VO3 @ 2500 mV")
    endif()
endif()
