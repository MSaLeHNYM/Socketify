add_executable(example_06_https main.cpp)
target_link_libraries(example_06_https PRIVATE Socketify::socketify)

# Ship the cert helper next to the binary.
add_custom_command(TARGET example_06_https POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/gen_cert.sh
        $<TARGET_FILE_DIR:example_06_https>/gen_cert.sh)
