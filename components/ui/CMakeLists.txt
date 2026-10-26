file(GLOB FONT_SRCS "${CMAKE_CURRENT_LIST_DIR}/fonts/*.c")
idf_component_register(SRCS "ui.c" "ui_main.c" "ui_chart.c" "ui_settings.c" ${FONT_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES afr_core)
