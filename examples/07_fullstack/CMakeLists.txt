add_executable(example_07_fullstack main.cpp)
target_link_libraries(example_07_fullstack PRIVATE Socketify::socketify)

add_custom_command(TARGET example_07_fullstack POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/public
        $<TARGET_FILE_DIR:example_07_fullstack>/public)
