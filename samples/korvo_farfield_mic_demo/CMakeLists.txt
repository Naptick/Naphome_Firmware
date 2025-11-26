cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/led_strip"
    "/Users/danielmcshan/GitHub/esp-sr")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo_farfield_mic_demo)
