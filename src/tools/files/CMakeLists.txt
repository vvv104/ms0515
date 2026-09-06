# ── ms0515-files ─────────────────────────────────────────────────────────────
# The standalone two-panel file manager for the terminal: the libraries in
# src/files/ (the model and the FTXUI panels) under a main() that runs them
# in FTXUI's own interactive loop.  The same panels appear inside
# ms0515-cli over the running machine.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(ms0515-files
    main.cpp
)

target_link_libraries(ms0515-files
    PRIVATE ms0515_files_ui
    PRIVATE ms0515_platform_cli
    PRIVATE ms0515_win7compat
)

# ── Package output ──────────────────────────────────────────────────────────
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/../package")
add_custom_command(TARGET ms0515-files POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515-files>" "${PACKAGE_DIR}/"
    COMMENT "Copying ms0515-files into ${PACKAGE_DIR}"
)
