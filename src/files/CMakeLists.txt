# ── ms0515_files / ms0515_files_ui ───────────────────────────────────────────
# The RT-11 file manager as libraries, so that both the standalone
# ms0515-files binary (tools/files/) and the commander inside ms0515-cli
# share them:
#   ms0515_files    — the model: a device's volume (Location), which image is
#                     behind which device (Mounts), the panel state, the
#                     operations between volumes and the host, the viewer,
#                     the key sequences.  No terminal, unit-tested.
#   ms0515_files_ui — the two panels, dialogs and viewer drawn with FTXUI.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_files STATIC
    Location.cpp
    Mounts.cpp
    Keys.cpp
    Panel.cpp
    Ops.cpp
    Viewer.cpp
)

target_include_directories(ms0515_files
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ms0515_files
    PUBLIC  ms0515_disk ms0515_libapp
    PRIVATE fmt::fmt
)

find_package(ftxui REQUIRED)

add_library(ms0515_files_ui STATIC
    Tui.cpp
)

target_link_libraries(ms0515_files_ui
    PUBLIC  ms0515_files ftxui::component ftxui::dom ftxui::screen
    PRIVATE fmt::fmt
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
