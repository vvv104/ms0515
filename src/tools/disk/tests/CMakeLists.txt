# ── ms0515-disk tests ─────────────────────────────────────────────────────
# The native disk wizard's screen, driven by events and read back from a
# rendered screen.
# ────────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_disk_tool_tests
    test_main.cpp
    test_wizard_tui.cpp
)

target_link_libraries(ms0515_disk_tool_tests
    PRIVATE
        ms0515_disk_wizard_ui
        doctest::doctest
)

add_test(NAME ms0515_disk_tool_tests COMMAND ms0515_disk_tool_tests)
