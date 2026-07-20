cmake_minimum_required(VERSION 3.16)

project(Socketify
    VERSION 0.2.0
    DESCRIPTION "Socketify: a fast, modern C++20 HTTP/HTTPS server & routing framework"
    HOMEPAGE_URL "https://github.com/yourname/socketify"
    LANGUAGES CXX
)

# ---------------------------
# Options
# ---------------------------
option(SOCKETIFY_WITH_TLS "Enable HTTPS (OpenSSL)" ON)
option(SOCKETIFY_BUILD_EXAMPLES "Build examples" OFF)
option(SOCKETIFY_BUILD_TESTS "Build tests" OFF)
option(SOCKETIFY_BUILD_DOCS "Add a 'docs' target (requires Doxygen)" OFF)

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

include("${CMAKE_CURRENT_LIST_DIR}/cmake/SocketifyWarnings.cmake")

# ---------------------------
# Dependencies
# ---------------------------
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(ZLIB REQUIRED)

if(SOCKETIFY_WITH_TLS)
    find_package(OpenSSL REQUIRED)
endif()

# ---------------------------
# Sources / Headers
# ---------------------------
set(SOCKETIFY_PUBLIC_HEADERS
    include/socketify/socketify.h
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
    include/socketify/detail/sse_impl.h
)

set(SOCKETIFY_SOURCES
    src/http.cpp
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
    src/detail/file_io_posix.cpp
    src/detail/utils.cpp
)

# ---------------------------
# Library Target
# ---------------------------
add_library(socketify ${SOCKETIFY_SOURCES} ${SOCKETIFY_PUBLIC_HEADERS})
add_library(Socketify::socketify ALIAS socketify)

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
        Threads::Threads
)

if(SOCKETIFY_WITH_TLS)
    target_compile_definitions(socketify PUBLIC SOCKETIFY_HAS_TLS=1)
    target_link_libraries(socketify PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
    target_compile_definitions(socketify PUBLIC SOCKETIFY_HAS_TLS=0)
endif()

socketify_set_warnings(socketify)
socketify_enable_sanitizers(socketify)

# ---------------------------
# Examples (optional)
# ---------------------------
if(SOCKETIFY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------
# Tests (optional)
# ---------------------------
if(SOCKETIFY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------
# Docs (optional)
# ---------------------------
if(SOCKETIFY_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/docs/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    else()
        message(WARNING "SOCKETIFY_BUILD_DOCS=ON but Doxygen was not found")
    endif()
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

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/packaging/SocketifyConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfig.cmake"
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SocketifyConfigVersion.cmake"
    DESTINATION ${INSTALL_CONFIGDIR}
)

message(STATUS "== Socketify ${PROJECT_VERSION} ==")
message(STATUS "  Build type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  TLS:         ${SOCKETIFY_WITH_TLS}")
message(STATUS "  Examples:    ${SOCKETIFY_BUILD_EXAMPLES}")
message(STATUS "  Tests:       ${SOCKETIFY_BUILD_TESTS}")
message(STATUS "  Docs:        ${SOCKETIFY_BUILD_DOCS}")
message(STATUS "  Sanitizers:  ${SOCKETIFY_SANITIZE}")
