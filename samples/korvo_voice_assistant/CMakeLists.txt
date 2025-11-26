cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Reuse shared drivers/components from the repository root.
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/aws_iot"
    "${PROJECT_ROOT}/components/esp_aws_iot"
    "${PROJECT_ROOT}/components/jsmn"
    "${PROJECT_ROOT}/components/somnus_mqtt"
    "${PROJECT_ROOT}/components/somnus_profile"
    "${PROJECT_ROOT}/components/cjson"
    "${PROJECT_ROOT}/components/led_strip"
    "${PROJECT_ROOT}/components/sensor_manager"
    "${PROJECT_ROOT}/components/matter_bridge"
    "${PROJECT_ROOT}/drivers/audio/korvo1"
    "${PROJECT_ROOT}/drivers/sensor/sht45"
    "${PROJECT_ROOT}/drivers/sensor/sgp40"
    "${PROJECT_ROOT}/drivers/sensor/scd40"
    "${PROJECT_ROOT}/drivers/sensor/vcnl4040"
    "${PROJECT_ROOT}/drivers/sensor/ec10")

# Load the OpenAI API key from ~/.env without ever committing it to the repo.
set(OPENAI_ENV_FILE "$ENV{HOME}/.env")
set(OPENAI_API_KEY "")
set(GEMINI_API_KEY "")
if(EXISTS "${OPENAI_ENV_FILE}")
    file(STRINGS "${OPENAI_ENV_FILE}" _env_lines)
    foreach(_line IN LISTS _env_lines)
        string(STRIP "${_line}" _trimmed)
        if(_trimmed MATCHES "^OPENAI_API_KEY=")
            string(REPLACE "OPENAI_API_KEY=" "" OPENAI_API_KEY "${_trimmed}")
            string(REGEX REPLACE "^['\"](.*)['\"]$" "\\1" OPENAI_API_KEY "${OPENAI_API_KEY}")
        elseif(_trimmed MATCHES "^GEMINI_API_KEY=")
            string(REPLACE "GEMINI_API_KEY=" "" GEMINI_API_KEY "${_trimmed}")
            string(REGEX REPLACE "^['\"](.*)['\"]$" "\\1" GEMINI_API_KEY "${GEMINI_API_KEY}")
        endif()
    endforeach()
endif()

if(NOT OPENAI_API_KEY)
    message(FATAL_ERROR "OPENAI_API_KEY not found in ~/.env (expected line OPENAI_API_KEY=sk-xxxx)")
endif()

if(NOT GEMINI_API_KEY)
    message(WARNING "GEMINI_API_KEY not found in ~/.env (Gemini features will be disabled)")
endif()

set(GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")

# Generate version info from git
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%H
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%ci
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        OUTPUT_VARIABLE FIRMWARE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_COMMIT_DATE "unknown")
    set(FIRMWARE_VERSION "dev-build")
endif()

# Get build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

# Generate version header
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/main/version_info.h.in"
    "${GENERATED_INCLUDE_DIR}/version_info.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/main/openai_secrets.h.in"
    "${GENERATED_INCLUDE_DIR}/openai_secrets.h"
    @ONLY
)

# Generate version information
set(VERSION_HEADER "${GENERATED_INCLUDE_DIR}/app_version.h")
# Get git commit hash
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()

# Get git commit date
execute_process(
    COMMAND git log -1 --format=%ci
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_DATE)
    set(GIT_COMMIT_DATE "unknown")
endif()

# Get build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

# Generate version header
file(WRITE "${VERSION_HEADER}" 
"#ifndef APP_VERSION_H
#define APP_VERSION_H

#define APP_VERSION_GIT_COMMIT \"${GIT_COMMIT_HASH}\"
#define APP_VERSION_GIT_DATE \"${GIT_COMMIT_DATE}\"
#define APP_VERSION_BUILD_TIME \"${BUILD_TIMESTAMP}\"
#define APP_VERSION_STRING \"${GIT_COMMIT_HASH} (${BUILD_TIMESTAMP})\"

#endif // APP_VERSION_H
")

# Configure Gemini secrets if key is available
if(GEMINI_API_KEY)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/main/gemini_secrets.h.in"
        "${GENERATED_INCLUDE_DIR}/gemini_secrets.h"
        @ONLY
    )
    add_compile_definitions(GEMINI_ENABLED=1)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo_voice_assistant)

set(KVA_SPIFFS_DIR "${CMAKE_CURRENT_LIST_DIR}/spiffs")
# Check if partition table has 'storage' partition by reading the CSV
set(PARTITION_TABLE_FILE "${CMAKE_CURRENT_LIST_DIR}/../../config/partitions.csv")
if(EXISTS "${KVA_SPIFFS_DIR}" AND EXISTS "${PARTITION_TABLE_FILE}")
    file(READ "${PARTITION_TABLE_FILE}" partition_csv)
    if(partition_csv MATCHES "storage")
        spiffs_create_partition_image(storage "${KVA_SPIFFS_DIR}" FLASH_IN_PROJECT)
    else()
        message(STATUS "Skipping SPIFFS image creation - no 'storage' partition in ${PARTITION_TABLE_FILE}")
    endif()
elseif(EXISTS "${KVA_SPIFFS_DIR}")
    # Try to create anyway if SPIFFS dir exists
    spiffs_create_partition_image(storage "${KVA_SPIFFS_DIR}" FLASH_IN_PROJECT)
else()
    message(WARNING "SPIFFS directory not found at ${KVA_SPIFFS_DIR}; storage partition will be empty.")
endif()
