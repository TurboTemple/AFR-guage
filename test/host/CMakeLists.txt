# Host build of afr_core: Unity tests and the replay command-line program.
#
#   cmake -S test/host -B build/host && cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
#
# Unity comes from ThrowTheSwitch with -DAFR_FETCH_UNITY=ON (CI does this).
# Without it, the small Unity-compatible header in unity_min/ is used, so the
# tests also run offline.
cmake_minimum_required(VERSION 3.16)
project(afr_core_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(AFR_FETCH_UNITY "Download Unity instead of using unity_min" OFF)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../components/afr_core afr_core)

if(AFR_FETCH_UNITY)
  include(FetchContent)
  FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0)
  FetchContent_MakeAvailable(unity)
  set(UNITY_LIB unity)
else()
  add_library(unity_min INTERFACE)
  target_include_directories(unity_min INTERFACE ${CMAKE_CURRENT_LIST_DIR}/unity_min)
  set(UNITY_LIB unity_min)
endif()

enable_testing()

set(TESTS
  test_conversion
  test_calibration
  test_led_decoder
  test_gauge_state
  test_peaks
  test_smoothing
  test_pipeline
  test_settings
  test_spartan_proto
  test_log_format
)

foreach(t ${TESTS})
  add_executable(${t} ${t}.c)
  target_link_libraries(${t} PRIVATE afr_core ${UNITY_LIB})
  target_compile_options(${t} PRIVATE -Wall -Wextra)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(afr_replay replay_main.c)
target_link_libraries(afr_replay PRIVATE afr_core)
target_compile_options(afr_replay PRIVATE -Wall -Wextra -Werror)

# A simulated drive through the full 200 Hz path, logged at 20 Hz, then
# replayed: tools/replay.py must reproduce its states, warnings and peaks.
add_executable(afr_simlog simlog_main.c)
target_link_libraries(afr_simlog PRIVATE afr_core)
target_compile_options(afr_simlog PRIVATE -Wall -Wextra -Werror)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  foreach(rate 10 20 50)
    add_test(NAME replay_simlog_${rate}hz
      COMMAND sh -c "$<TARGET_FILE:afr_simlog> --rate ${rate} > simlog_${rate}.csv && ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tools/replay.py simlog_${rate}.csv --bin $<TARGET_FILE:afr_replay>")
  endforeach()
endif()
