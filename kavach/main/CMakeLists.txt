# Kavach minimal: voice recognition + text/light UI only.
file(GLOB_RECURSE ALL_SRCS
    "*.c"
    "app/*.c"
    "gui/*.c"
    "gui/font/*.c"
)

# Exclude RainMaker and unused UI/app
list(FILTER ALL_SRCS EXCLUDE REGEX ".*/rmaker/.*")
list(FILTER ALL_SRCS EXCLUDE REGEX ".*/(app_led|app_fan|app_switch|app_wifi|file_manager)\\.c$")
list(FILTER ALL_SRCS EXCLUDE REGEX ".*/(ui_main|ui_about_us|ui_boot_animate|ui_device_ctrl|ui_factory_mode|ui_hint|ui_mute|ui_net_config|ui_player|ui_sensor_monitor|ui_sr)\\.c$")
list(FILTER ALL_SRCS EXCLUDE REGEX ".*/gui/image/.*")
# Fonts for elder-friendly UI: en_12 (small labels), en_24, en_64 (big clock), en_bold_36 (title/status)
list(FILTER ALL_SRCS EXCLUDE REGEX ".*/font/(font_app|font_cn|font_en_14|font_en_16|font_en_22|font_hint|font_icon).*\\.c$")

idf_component_register(
    SRCS ${ALL_SRCS}
    INCLUDE_DIRS "." "app" "gui"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-deprecated-declarations")

set_source_files_properties(
    ${ALL_SRCS}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
