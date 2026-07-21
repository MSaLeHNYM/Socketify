# Socketify test suite (GoogleTest via FetchContent).

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Prevent GoogleTest from overriding compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(socketify_tests
    main.cpp
    unit/http_parser_tests.cpp
    unit/router_tests.cpp
    unit/utils_tests.cpp
    unit/cookies_tests.cpp
    unit/body_tests.cpp
    unit/compression_tests.cpp
    unit/cors_tests.cpp
    unit/ratelimit_tests.cpp
    unit/sessions_tests.cpp
    unit/logging_tests.cpp
    unit/version_tests.cpp
    unit/json_tests.cpp
    unit/validate_tests.cpp
    unit/config_tests.cpp
    unit/cache_tests.cpp
    unit/db_tests.cpp
    unit/pulse_frame_tests.cpp
    unit/pulse_enhanced_tests.cpp
    unit/static_files_tests.cpp
    unit/response_tests.cpp
    integration/server_integration_tests.cpp
    integration/sse_integration_tests.cpp
    integration/pulse_integration_tests.cpp
    integration/http_client_integration_tests.cpp
    integration/tls_integration_tests.cpp
)

target_link_libraries(socketify_tests
    PRIVATE
        Socketify::socketify
        GTest::gtest
        GTest::gmock
)

target_include_directories(socketify_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})

gtest_discover_tests(socketify_tests
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 60
)

# Smoke: installed-style CLI --version (when SOCKETIFY_BUILD_CLI is on).
if(TARGET socketify_cli)
    add_test(NAME CliVersion
        COMMAND $<TARGET_FILE:socketify_cli> --version)
    set_tests_properties(CliVersion PROPERTIES
        PASS_REGULAR_EXPRESSION "Socketify [0-9]+\\.[0-9]+\\.[0-9]+")
endif()
