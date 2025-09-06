cmake_minimum_required(VERSION 3.14)

# Give your project a distinctive name (this also names the library)
project(socketify LANGUAGES CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where to put the compiled artefacts
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include headers for compilation
include_directories(${CMAKE_SOURCE_DIR}/header)

# Collect all header and source files recursively
file(GLOB_RECURSE SOCKETIFY_HEADERS ${CMAKE_SOURCE_DIR}/header/*.h ${CMAKE_SOURCE_DIR}/header/json.hpp)
file(GLOB_RECURSE SOCKETIFY_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Build a static library named 'socketify'
add_library(socketify STATIC ${SOCKETIFY_SOURCES} ${SOCKETIFY_HEADERS})

# Provide a namespaced alias for users of the library
add_library(socketify::socketify ALIAS socketify)

# Install the compiled library artefacts
install(TARGETS socketify
        EXPORT socketifyTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

# Install all header files except json.hpp
install(DIRECTORY ${CMAKE_SOURCE_DIR}/header/
        DESTINATION include/socketify
        FILES_MATCHING PATTERN "*.h"
        PATTERN "json.hpp" EXCLUDE)

# Install json.hpp separately into the same include folder
install(FILES ${CMAKE_SOURCE_DIR}/header/json.hpp
        DESTINATION include/socketify)

# Export configuration so downstream projects can use find_package(socketify)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY AnyNewerVersion
)
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/socketifyConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfig.cmake"
    INSTALL_DESTINATION lib/cmake/socketify
)
install(EXPORT socketifyTargets
        FILE socketifyTargets.cmake
        NAMESPACE socketify::
        DESTINATION lib/cmake/socketify)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/socketifyConfigVersion.cmake"
    DESTINATION lib/cmake/socketify
)
