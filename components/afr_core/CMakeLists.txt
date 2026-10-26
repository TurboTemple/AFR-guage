# afr_core builds as an ESP-IDF component, or as a plain static library for
# the host tests (test/host).
set(AFR_CORE_SRCS
  alarms.c
  afr_settings.c
  calibration.c
  conversion.c
  gauge_state.c
  led_decoder.c
  log_format.c
  peaks.c
  pipeline.c
  smoothing.c
  spartan_proto.c
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${AFR_CORE_SRCS} INCLUDE_DIRS ".")
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wdouble-promotion -Wshadow)
else()
  add_library(afr_core STATIC ${AFR_CORE_SRCS})
  target_include_directories(afr_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
  target_compile_features(afr_core PUBLIC c_std_11)
  target_compile_options(afr_core PRIVATE -Wall -Wextra -Wshadow -Wdouble-promotion -Werror)
  target_link_libraries(afr_core PUBLIC m)
endif()
