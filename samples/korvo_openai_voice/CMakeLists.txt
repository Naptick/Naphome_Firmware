cmake_minimum_required(VERSION 3.16)

# Reuse shared drivers/components from the repository root.
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/drivers"
    "${PROJECT_ROOT}/components")

# Load the OpenAI API key from ~/.env without ever committing it to the repo.
set(OPENAI_ENV_FILE "$ENV{HOME}/.env")
set(OPENAI_API_KEY "")
if(EXISTS "${OPENAI_ENV_FILE}")
    file(STRINGS "${OPENAI_ENV_FILE}" _env_lines)
    foreach(_line IN LISTS _env_lines)
        string(STRIP "${_line}" _trimmed)
        if(_trimmed MATCHES "^OPENAI_API_KEY=")
            string(REPLACE "OPENAI_API_KEY=" "" OPENAI_API_KEY "${_trimmed}")
            string(REGEX REPLACE "^['\"](.*)['\"]$" "\\1" OPENAI_API_KEY "${OPENAI_API_KEY}")
            break()
        endif()
    endforeach()
endif()

if(NOT OPENAI_API_KEY)
    message(FATAL_ERROR "OPENAI_API_KEY not found in ~/.env (expected line OPENAI_API_KEY=sk-xxxx)")
endif()

set(GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/main/openai_secrets.h.in"
    "${GENERATED_INCLUDE_DIR}/openai_secrets.h"
    @ONLY
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo_openai_voice)
