cmake_minimum_required(VERSION 3.14)
project(socketify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include headers (for build)
include_directories(${CMAKE_SOURCE_DIR}/header)

# Source files
file(GLOB_RECURSE SOCKETIFY_HEADERS ${CMAKE_SOURCE_DIR}/header/*.h ${CMAKE_SOURCE_DIR}/header/json.hpp)
file(GLOB_RECURSE SOCKETIFY_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Create static library
add_library(socketify STATIC ${SOCKETIFY_SOURCES} ${SOCKETIFY_HEADERS})

# Alias for usage by downstream projects
add_library(socketify::socketify ALIAS socketify)

# Install library
install(TARGETS socketify
        EXPORT socketifyTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

# Install all headers EXCEPT json.hpp
install(DIRECTORY ${CMAKE_SOURCE_DIR}/header/
        DESTINATION include/socketify
        FILES_MATCHING PATTERN "*.h"
        PATTERN "json.hpp" EXCLUDE)

# Install json.hpp separately (to the same include folder)
install(FILES ${CMAKE_SOURCE_DIR}/header/json.hpp
        DESTINATION include/socketify)

# Export for find_package support
install(EXPORT socketifyTargets
        FILE socketifyTargets.cmake
        NAMESPACE socketify::
        DESTINATION lib/cmake/socketify)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY AnyNewerVersion
)

# Configure package config file for find_package()
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/socketifyConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfig.cmake"
    INSTALL_DESTINATION lib/cmake/socketify
)

# Install the config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfigVersion.cmake"
    DESTINATION lib/cmake/socketify
)
