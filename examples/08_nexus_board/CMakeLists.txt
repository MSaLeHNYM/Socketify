cmake_minimum_required(VERSION 3.16)
enable_language(C)

add_executable(example_08_nexus_board
    backend/main.cpp
    backend/db.cpp
    third_party/sqlite/sqlite3.c
)

target_include_directories(example_08_nexus_board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

target_compile_definitions(example_08_nexus_board PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
)

set_source_files_properties(third_party/sqlite/sqlite3.c PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-conversion -Wno-implicit-fallthrough -Wno-conversion"
)

target_link_libraries(example_08_nexus_board PRIVATE Socketify::socketify ${CMAKE_DL_LIBS})

set(NEXUS_DIST "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist")

add_custom_command(TARGET example_08_nexus_board POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/data
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/uploads
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:example_08_nexus_board>/web
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/web
    COMMAND ${CMAKE_COMMAND}
        -DDEST=$<TARGET_FILE_DIR:example_08_nexus_board>/web
        -DDIST=${NEXUS_DIST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/stage_web.cmake
    COMMENT "Staging Nexus web UI next to example_08_nexus_board"
)
