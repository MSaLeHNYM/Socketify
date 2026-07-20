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

# Generate the staging helper into the build tree so a missing source-side
# helper file can never break the link step.
set(_nexus_stage "${CMAKE_CURRENT_BINARY_DIR}/stage_web.sh")
file(WRITE "${_nexus_stage}" [=[
#!/usr/bin/env sh
set -eu
OUT="${1:?}"
DIST="${2:?}"
mkdir -p "${OUT}"
if [ -f "${DIST}/index.html" ]; then
  cp -a "${DIST}/." "${OUT}/"
else
  cat > "${OUT}/index.html" <<'HTML'
<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>Nexus Board</title></head>
<body style="font-family:system-ui;margin:2rem;max-width:40rem">
  <h1>Build the React app</h1>
  <pre>cd examples/08_nexus_board/frontend
npm i && npm run build</pre>
  <p>Then rebuild <code>example_08_nexus_board</code>.</p>
</body>
</html>
HTML
fi
]=])
file(CHMOD "${_nexus_stage}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

add_custom_command(TARGET example_08_nexus_board POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/data
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/uploads
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:example_08_nexus_board>/web
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:example_08_nexus_board>/web
    COMMAND "${_nexus_stage}"
            $<TARGET_FILE_DIR:example_08_nexus_board>/web
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist
    COMMENT "Staging Nexus web UI next to example_08_nexus_board"
)
