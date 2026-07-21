cmake_minimum_required(VERSION 3.16)
add_executable(example_11_pulse_media main.cpp)
target_link_libraries(example_11_pulse_media PRIVATE Socketify::socketify)
