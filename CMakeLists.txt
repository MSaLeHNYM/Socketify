cmake_minimum_required(VERSION 3.14)
project(Socketify LANGUAGES CXX)

# C++17 Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source and header files
file(GLOB_RECURSE HEADERS "header/*.h")
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create static library
add_library(socketify STATIC ${SOURCES} ${HEADERS})

# Include headers (build + install)
target_include_directories(socketify
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/header>
        $<INSTALL_INTERFACE:include>
)

# Install the static library
install(TARGETS socketify
    EXPORT socketifyTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY header/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/)

# Install CMake config export
install(EXPORT socketifyTargets
    FILE socketifyConfig.cmake
    NAMESPACE socketify::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/socketify
)
