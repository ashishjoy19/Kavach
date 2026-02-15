# Kavach minimal: voice recognition + text/light UI only.
file(GLOB_RECURSE ALL_SRCS
    "*.c"
    "app/*.c"
    "gui/*.c"
    "gui/font/*.c"
)

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
