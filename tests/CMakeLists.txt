cmake_minimum_required(VERSION 3.16)
project(socketify_tests LANGUAGES CXX)

find_package(GTest REQUIRED)

add_executable(http_router_tests http_router_tests.cpp)
target_link_libraries(http_router_tests PRIVATE Socketify::socketify GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(http_router_tests)