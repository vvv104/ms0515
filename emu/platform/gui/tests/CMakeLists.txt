# ── ms0515_platform_gui unit tests ───────────────────────────────────────────
# Smoke tests for the platform abstraction.  Most paths only have visible
# effects on a GUI-subsystem binary (AttachConsole, stdout rebind); the test
# runner is a console-subsystem binary, so we mainly verify the symbols are
# linkable, the functions don't crash, and the idempotent guards hold.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_platform_gui_tests
    test_main.cpp
    test_platform.cpp
)

target_compile_features(ms0515_platform_gui_tests PRIVATE cxx_std_23)

target_link_libraries(ms0515_platform_gui_tests
    PRIVATE ms0515_platform_gui
    PRIVATE doctest::doctest
)

add_test(NAME ms0515_platform_gui_tests COMMAND ms0515_platform_gui_tests)
