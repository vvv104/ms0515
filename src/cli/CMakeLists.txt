# ── ms0515_cli ─────────────────────────────────────────────────────────────
# Text-mode frontend.  Boots the real Omega RT-11 kernel via src/lib/,
# replacing CRT + keyboard with a stdio bridge installed via the
# cpu->trap_thunk hook (added in Stage 1).  No SDL, no ImGui — just a
# console process that exposes the kernel as if it were a serial session.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(ms0515-cli
    src/main.cpp
    src/Koi8.cpp
    src/StdioBridge.cpp
)

target_include_directories(ms0515-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ms0515-cli
    PRIVATE ms0515_lib
    PRIVATE ms0515_libapp
    PRIVATE ms0515_platform_cli
    PRIVATE ms0515_win7compat
)

# ── Package output ──────────────────────────────────────────────────────────
# Drop the .exe next to ms0515.exe in package/ so users get both binaries
# side-by-side (sharing assets/ and ms0515.yaml).  The frontend's POST_BUILD
# step is what creates package/ and copies assets in; we only need to add
# our exe.
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/../package")
add_custom_command(TARGET ms0515-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515-cli>" "${PACKAGE_DIR}/"
    COMMENT "Copying ms0515-cli into ${PACKAGE_DIR}"
)
