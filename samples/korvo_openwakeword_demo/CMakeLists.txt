cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/openwakeword"
    "${PROJECT_ROOT}/components/led_strip"
    "${PROJECT_ROOT}/drivers/audio/korvo1"
)

# Add korvo_audio source
set(KORVO_AUDIO_DIR "${PROJECT_ROOT}/samples/korvo_voice_assistant/main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo_openwakeword_demo)