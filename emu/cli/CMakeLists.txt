# ── ms0515_cli ─────────────────────────────────────────────────────────────
# Text-mode frontend.  Boots the real Omega RT-11 kernel via emu/lib/,
# replacing CRT + keyboard with a stdio bridge installed via the
# cpu->trap_thunk hook (added in Stage 1).  No SDL, no ImGui — just a
# console process that exposes the kernel as if it were a serial session.
# ─────────────────────────────────────────────────────────────────────────────

if(WIN32)
    set(CLI_PLATFORM_SRC src/Platform_win32.cpp)
else()
    set(CLI_PLATFORM_SRC src/Platform_unix.cpp)
endif()

add_executable(ms0515-cli
    src/main.cpp
    src/Koi8.cpp
    src/StdioBridge.cpp
    ${CLI_PLATFORM_SRC}
)

target_compile_features(ms0515-cli PRIVATE cxx_std_23)

target_include_directories(ms0515-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ms0515-cli PRIVATE
    MS0515_DEFAULT_ROM="${CMAKE_SOURCE_DIR}/assets/rom/ms0515-romb.rom"
)

target_link_libraries(ms0515-cli
    PRIVATE ms0515_lib
)
