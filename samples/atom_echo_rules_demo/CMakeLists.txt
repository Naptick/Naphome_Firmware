cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

# Load the Gemini API key from project .env file
set(GEMINI_ENV_FILE "${PROJECT_ROOT}/.env")
set(GEMINI_API_KEY "")
if(EXISTS "${GEMINI_ENV_FILE}")
    file(STRINGS "${GEMINI_ENV_FILE}" _env_lines)
    foreach(_line IN LISTS _env_lines)
        string(STRIP "${_line}" _trimmed)
        if(_trimmed MATCHES "^GEMINI_API_KEY=")
            string(REPLACE "GEMINI_API_KEY=" "" GEMINI_API_KEY "${_trimmed}")
            string(REGEX REPLACE "^['\"](.*)['\"]$" "\\1" GEMINI_API_KEY "${GEMINI_API_KEY}")
            break()
        endif()
    endforeach()
endif()

if(GEMINI_API_KEY)
    message(STATUS "Gemini API key loaded from .env file")
    # Set as compile-time definition
    add_compile_definitions(GEMINI_API_KEY="${GEMINI_API_KEY}")
else()
    message(WARNING "GEMINI_API_KEY not found in .env (Gemini features will be disabled)")
endif()
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/aws_iot"
    "${PROJECT_ROOT}/components/cspot_component"
    "${PROJECT_ROOT}/components/cjson"
    "${PROJECT_ROOT}/components/esp_aws_iot"
    "${PROJECT_ROOT}/components/jsmn"
    "${PROJECT_ROOT}/components/led_strip"
    "${PROJECT_ROOT}/components/matter_bridge"
    "${PROJECT_ROOT}/components/m5gfx"
    "${PROJECT_ROOT}/components/sensor_manager"
    "${PROJECT_ROOT}/components/somnus_ble"
    "${PROJECT_ROOT}/components/somnus_mqtt"
    "${PROJECT_ROOT}/components/somnus_profile"
    "${PROJECT_ROOT}/components/wifi_manager"
    "${PROJECT_ROOT}/drivers/audio/korvo1"
    "${PROJECT_ROOT}/drivers/sensor/sht45"
    "${PROJECT_ROOT}/drivers/sensor/sgp40"
    "${PROJECT_ROOT}/drivers/sensor/scd40"
    "${PROJECT_ROOT}/drivers/sensor/vcnl4040"
    "${PROJECT_ROOT}/drivers/sensor/ec10")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atom_echo_rules_demo)
