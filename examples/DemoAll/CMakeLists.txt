cmake_minimum_required(VERSION 3.16)

# Demo that uses CORS, Static Files, and API routes
add_executable(demo_all demo_all.cpp)
target_link_libraries(demo_all PRIVATE Socketify::socketify)
