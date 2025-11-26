set(CJSON_SOURCE_DIR "$ENV{IDF_PATH}/components/json/cJSON")
if(NOT EXISTS "${CJSON_SOURCE_DIR}/cJSON.c")
    message(FATAL_ERROR "cJSON sources not found at ${CJSON_SOURCE_DIR}")
endif()

set(CJSON_SOURCES "${CJSON_SOURCE_DIR}/cJSON.c")
if(EXISTS "${CJSON_SOURCE_DIR}/cJSON_Utils.c")
    list(APPEND CJSON_SOURCES "${CJSON_SOURCE_DIR}/cJSON_Utils.c")
endif()

idf_component_register(
    SRCS ${CJSON_SOURCES}
    INCLUDE_DIRS "${CJSON_SOURCE_DIR}"
)
