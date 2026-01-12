idf_component_register()

# Disable cspot component for now - requires external repository
return()

if(NOT CONFIG_CSPOT_ENABLE)
    message(STATUS "cspot component disabled via Kconfig")
    return()
endif()

set(CSPOT_ROOT "${CONFIG_CSPOT_PATH}")
if(CSPOT_ROOT MATCHES "^~")
    string(REGEX REPLACE "^~" "$ENV{HOME}" CSPOT_ROOT "${CSPOT_ROOT}")
endif()
get_filename_component(CSPOT_ROOT "${CSPOT_ROOT}" ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_CXX_FLAGS MATCHES "-std=gnu\\+11")
    string(REPLACE "-std=gnu++11" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
if(CMAKE_CXX_FLAGS MATCHES "-fno-exceptions")
    string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
if(CMAKE_CXX_FLAGS MATCHES "-fno-rtti")
    string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

if(NOT EXISTS "${CSPOT_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "cspot component: CMakeLists.txt not found at ${CSPOT_ROOT}. "
                        "Set CONFIG_CSPOT_PATH to your cspot checkout (e.g. ~/GitHub/cspot).")
endif()

set(CSPOT_BINARY_DIR "${CMAKE_BINARY_DIR}/cspot_external")
add_subdirectory("${CSPOT_ROOT}/cspot" "${CSPOT_BINARY_DIR}" EXCLUDE_FROM_ALL)

set_target_properties(bell PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
set_target_properties(cspot PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(bell PRIVATE -fexceptions -frtti -std=gnu++17)
target_compile_options(cspot PRIVATE -fexceptions -frtti -std=gnu++17)

target_link_libraries(${COMPONENT_LIB} INTERFACE cspot bell)
target_compile_definitions(${COMPONENT_LIB} INTERFACE KVA_HAVE_CSPOT=1 CONFIG_KVA_SPOTIFY_USE_CSPOT=1)
