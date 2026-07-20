add_executable(example_04_static_site main.cpp)
target_link_libraries(example_04_static_site PRIVATE Socketify::socketify)

# Copy the demo site next to the binary so relative paths work.
add_custom_command(TARGET example_04_static_site POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/public
        $<TARGET_FILE_DIR:example_04_static_site>/public)
