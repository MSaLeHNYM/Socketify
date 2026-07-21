# Socketify manager CLI (installed as `socketify`).

add_executable(socketify_cli main.cpp)
set_target_properties(socketify_cli PROPERTIES OUTPUT_NAME socketify)
target_link_libraries(socketify_cli PRIVATE Socketify::socketify)
socketify_set_warnings(socketify_cli)

include(GNUInstallDirs)
install(TARGETS socketify_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
