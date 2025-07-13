cmake_minimum_required(VERSION 3.14)
project(Socketify LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional: install path
include(GNUInstallDirs)

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
)
FetchContent_MakeAvailable(json)

# Headers and sources
file(GLOB_RECURSE HEADERS "header/*.h")
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create library
add_library(socketify STATIC ${HEADERS} ${SOURCES})
target_include_directories(socketify PUBLIC header)
target_link_libraries(socketify PUBLIC nlohmann_json::nlohmann_json)

# Install targets (optional)
install(TARGETS socketify
        EXPORT socketifyTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY header/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export the package (optional)
install(EXPORT socketifyTargets
        FILE socketifyConfig.cmake
        NAMESPACE socketify::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/socketify)
