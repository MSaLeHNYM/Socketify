cmake_minimum_required(VERSION 3.16)

project(Socketify
    VERSION 0.1.0
    DESCRIPTION "Socketify: a lightweight C++ HTTP/HTTPS server & routing library"
    LANGUAGES CXX
)

# ---------------------------
# Options
# ---------------------------
option(SOCKETIFY_WITH_TLS "Enable HTTPS (OpenSSL)" ON)
option(SOCKETIFY_WITH_BROTLI "Enable Brotli compression (not used in v1)" OFF)
# Default OFF to avoid linking empty examples with no main()
option(SOCKETIFY_BUILD_EXAMPLES "Build examples" OFF)
option(SOCKETIFY_BUILD_TESTS "Build tests" OFF)

# Respect BUILD_SHARED_LIBS (default static)
if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF)
endif()

# ---------------------------
# Toolchain / warnings
# ---------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/cmake/SocketifyWarnings.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/SocketifyWarnings.cmake")
endif()

# ---------------------------
# Dependencies
# ---------------------------
find_package(nlohmann_json 3.11 REQUIRED)
find_package(ZLIB REQUIRED)

if(SOCKETIFY_WITH_TLS)
    find_package(OpenSSL REQUIRED)
endif()

# ---------------------------
# Sources / Headers
# ---------------------------
set(SOCKETIFY_PUBLIC_HEADERS
    include/socketify/http.h
    include/socketify/server.h
    include/socketify/request.h
    include/socketify/response.h
    include/socketify/router.h
    include/socketify/middleware.h
    include/socketify/cors.h
    include/socketify/compression.h
    include/socketify/static_files.h
    include/socketify/body.h
    include/socketify/cookies.h
    include/socketify/sessions.h
    include/socketify/ratelimit.h
    include/socketify/logging.h
    include/socketify/tls.h
    include/socketify/sse.h
    include/socketify/detail/loop.h
    include/socketify/detail/socket.h
    include/socketify/detail/http_parser.h
    include/socketify/detail/buffer.h
    include/socketify/detail/file_io.h
    include/socketify/detail/utils.h
)

set(SOCKETIFY_SOURCES
    src/http.cpp                # <-- ADDED: implement http.h
    src/server.cpp
    src/router.cpp
    src/request.cpp
    src/response.cpp
    src/middleware.cpp
    src/cors.cpp
    src/compression.cpp
    src/static_files.cpp
    src/body.cpp
    src/cookies.cpp
    src/sessions.cpp
    src/ratelimit.cpp
    src/logging.cpp
    src/tls.cpp
    src/sse.cpp
    src/detail/loop_epoll.cpp
    src/detail/socket_posix.cpp
    src/detail/http_parser_sm.cpp
    src/detail/buffer.cpp
    src/detail/file_io_posix.cpp
)

# ---------------------------
# Library Target
# ---------------------------
add_library(socketify ${SOCKETIFY_SOURCES} ${SOCKETIFY_PUBLIC_HEADERS})

target_include_directories(socketify
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_features(socketify PUBLIC cxx_std_20)

target_link_libraries(socketify
    PUBLIC
        nlohmann_json::nlohmann_json
        ZLIB::ZLIB
)

if(SOCKETIFY_WITH_TLS)
    target_compile_definitions(socketify PUBLIC SOCKETIFY_HAS_TLS=1)
    target_link_libraries(socketify PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
    target_compile_definitions(socketify PUBLIC SOCKETIFY_HAS_TLS=0)
endif()

if(MSVC)
    target_compile_options(socketify PRIVATE /W4 /permissive-)
else()
    target_compile_options(socketify PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(Socketify::socketify ALIAS socketify)

# ---------------------------
# Examples (optional)
# ---------------------------
if(SOCKETIFY_BUILD_EXAMPLES)
    # Build flat examples if the files exist
    foreach(_ex minimal_api json_echo static_server)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/examples/${_ex}.cpp")
            add_executable(${_ex} "examples/${_ex}.cpp")
            target_link_libraries(${_ex} PRIVATE Socketify::socketify)
        endif()
    endforeach()

    # Build EVERY subdirectory in examples/ that contains a CMakeLists.txt
    file(GLOB _example_subdirs LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/examples/*")
    foreach(_dir ${_example_subdirs})
        if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/CMakeLists.txt")
            # Derive a nice name like "examples/HelloWorld"
            file(RELATIVE_PATH _reldir "${CMAKE_CURRENT_LIST_DIR}" "${_dir}")
            message(STATUS "Adding example subdir: ${_reldir}")
            add_subdirectory("${_dir}")
        endif()
    endforeach()
endif()



# ---------------------------
# Tests (optional)
# ---------------------------
if(SOCKETIFY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------
# Install
# ---------------------------
include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/Socketify)

install(TARGETS socketify
    EXPORT SocketifyTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

install(EXPORT SocketifyTargets
    FILE SocketifyTargets.cmake
    NAMESPACE Socketify::
    DESTINATION ${INSTALL_CONFIGDIR}
)

# ---------------------------
# Package Config generation
# ---------------------------
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

set(_SOCKETIFY_CONFIG_IN "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfig.cmake.in")
file(WRITE "${_SOCKETIFY_CONFIG_IN}"
"@PACKAGE_INIT@
include(\"\${CMAKE_CURRENT_LIST_DIR}/SocketifyTargets.cmake\")
check_required_components(Socketify)
")

configure_package_config_file(
    "${_SOCKETIFY_CONFIG_IN}"
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfig.cmake"
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfigVersion.cmake"
    DESTINATION ${INSTALL_CONFIGDIR}
)

message(STATUS "== Socketify ${PROJECT_VERSION} ==")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  TLS: ${SOCKETIFY_WITH_TLS}")
message(STATUS "  Examples: ${SOCKETIFY_BUILD_EXAMPLES}")
message(STATUS "  Tests: ${SOCKETIFY_BUILD_TESTS}")

#sudo apt update
#sudo apt install -y build-essential cmake libssl-dev zlib1g-dev nlohmann-json3-dev

# Build static lib (default), with TLS, and examples:
#cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build shared lib (.so) without TLS and without examples:
#cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#      -DBUILD_SHARED_LIBS=ON \
#      -DSOCKETIFY_WITH_TLS=OFF \
#      -DSOCKETIFY_BUILD_EXAMPLES=OFF

# Build tests too:
#cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSOCKETIFY_BUILD_TESTS=ON


# Install to /usr/local
#sudo cmake --install build

# Use from another project:
#   find_package(Socketify REQUIRED)
#   target_link_libraries(your_app PRIVATE Socketify::socketify)
