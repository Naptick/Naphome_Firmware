# Naphome Phase 0.9 Firmware - Main CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version
project(naphome-firmware VERSION 0.9.0)

# Set default components
set(EXTRA_COMPONENT_DIRS "components")

# Build configuration
set(COMPONENT_DIRS 
    "drivers"
    "components"
)
