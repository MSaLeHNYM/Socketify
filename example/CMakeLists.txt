cmake_minimum_required(VERSION 3.14)
project(SocketifyMainTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the installed Socketify library
find_package(socketify REQUIRED)

# Define the example executable
add_executable(socketify_main_test main-test.cpp)

# Link with the installed Socketify library
target_link_libraries(socketify_main_test PRIVATE socketify::socketify)
