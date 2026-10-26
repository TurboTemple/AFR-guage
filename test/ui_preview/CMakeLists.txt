# Renders the UI on the PC with the real LVGL 9.4 and the firmware's ui
# component, for checking layouts without the board:
#
#   cmake -S test/ui_preview -B build/ui_preview && cmake --build build/ui_preview
#   mkdir -p build/frames && build/ui_preview/ui_preview build/frames
#   python3 tools/ui_frames_to_png.py build/frames
cmake_minimum_required(VERSION 3.16)
project(ui_preview C)

include(FetchContent)
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.4.0)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()
file(GLOB_RECURSE LV_SRCS ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl STATIC ${LV_SRCS})
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${lvgl_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -O1 -w)

set(G ${CMAKE_CURRENT_LIST_DIR}/../../components)
add_subdirectory(${G}/afr_core afr_core)
file(GLOB UI_SRCS ${G}/ui/*.c ${G}/ui/fonts/*.c)
add_executable(ui_preview preview_main.c ${UI_SRCS})
target_include_directories(ui_preview PRIVATE ${G}/ui)
target_link_libraries(ui_preview PRIVATE lvgl afr_core m)
target_compile_options(ui_preview PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
