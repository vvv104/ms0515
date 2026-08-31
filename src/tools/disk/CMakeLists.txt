# ── ms0515-disk ──────────────────────────────────────────────────────────────
# Offline RT-11 / MS-0515 disk utility (dir / extract / build) over the
# ms0515_disk library.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(ms0515-disk
    main.cpp
)

target_link_libraries(ms0515-disk
    PRIVATE ms0515_disk
    PRIVATE ms0515_win7compat
)

# ── Package output ──────────────────────────────────────────────────────────
# Drop the .exe next to ms0515.exe / ms0515-cli.exe so the release archive
# ships all three binaries side-by-side (sharing assets/ and ms0515.yaml).
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/../package")
add_custom_command(TARGET ms0515-disk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515-disk>" "${PACKAGE_DIR}/"
    COMMENT "Copying ms0515-disk into ${PACKAGE_DIR}"
)
