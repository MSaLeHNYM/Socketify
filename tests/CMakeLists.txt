cmake_minimum_required(VERSION 3.16)
project(socketify_tests LANGUAGES CXX)

add_executable(http_router_tests http_router_tests.cpp)
target_link_libraries(http_router_tests PRIVATE Socketify::socketify)
add_test(NAME http_router_tests COMMAND http_router_tests)