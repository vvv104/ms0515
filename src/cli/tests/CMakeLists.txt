# ── ms0515_cli_core unit tests ───────────────────────────────────────────────
# The commander's glue into the CLI, exercised without a terminal: the
# mounts applied to a real Emulator (on scratch copies of the fixture
# disks) and the machine's screen drawn as FTXUI rows.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_cli_tests
    test_main.cpp
    test_mount_sync.cpp
    test_guest_screen.cpp
)

target_compile_definitions(ms0515_cli_tests PRIVATE
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FIXTURE_DISKS_DIR="${CMAKE_SOURCE_DIR}/lib/tests/disks"
)

target_link_libraries(ms0515_cli_tests
    PRIVATE
        ms0515_cli_core
        doctest::doctest
)

add_test(NAME ms0515_cli_tests COMMAND ms0515_cli_tests)
