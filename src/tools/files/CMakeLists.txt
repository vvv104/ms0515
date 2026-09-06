# ── ms0515-files ─────────────────────────────────────────────────────────────
# A two-panel file manager for the terminal over the ms0515_disk library:
# host directories and the volumes inside disk images side by side, with
# the web commander's keys.  The panel model, the operations and the
# viewer are a library of their own (ms0515_files_core, no terminal), so
# they are unit-tested; the FTXUI front-end and main() are the thin part.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_files_core STATIC
    Location.cpp
    Mounts.cpp
    Panel.cpp
    Ops.cpp
    Viewer.cpp
)

target_include_directories(ms0515_files_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ms0515_files_core
    PUBLIC  ms0515_disk ms0515_libapp
    PRIVATE fmt::fmt
)

find_package(ftxui REQUIRED)

add_executable(ms0515-files
    main.cpp
    Tui.cpp
)

target_link_libraries(ms0515-files
    PRIVATE ms0515_files_core
    PRIVATE ms0515_platform_cli
    PRIVATE ftxui::component ftxui::dom ftxui::screen
    PRIVATE fmt::fmt
    PRIVATE ms0515_win7compat
)

# ── Package output ──────────────────────────────────────────────────────────
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/../package")
add_custom_command(TARGET ms0515-files POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515-files>" "${PACKAGE_DIR}/"
    COMMENT "Copying ms0515-files into ${PACKAGE_DIR}"
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
