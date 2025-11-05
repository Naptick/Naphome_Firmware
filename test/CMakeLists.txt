# Test configuration
idf_build_get_property(idf_path IDF_PATH)

# Add Unity test framework
include(${idf_path}/tools/cmake/utilities.cmake)
require_idf_component(UNITY)

# Add CMock for mocking
require_idf_component(CMOCK)

# Add test directories
add_subdirectory(drivers)
