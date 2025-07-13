cmake_minimum_required(VERSION 3.14)
project(SocketifyExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the installed Socketify library
find_package(socketify REQUIRED)

# Define your example
add_executable(socketify_example main.cpp)

# Link with the installed socketify library
target_link_libraries(socketify_example PRIVATE socketify::socketify)
