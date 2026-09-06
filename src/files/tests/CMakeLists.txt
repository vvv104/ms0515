# ── ms0515_files unit tests ─────────────────────────────────────────────
# The file manager's model, exercised on scratch
# copies of the lib's fixture disks and on scratch host directories under
# the build tree - no terminal, no emulator.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_files_tests
    test_main.cpp
    test_location.cpp
    test_mounts.cpp
    test_keys.cpp
    test_panel.cpp
    test_ops.cpp
    test_viewer.cpp
)

target_compile_definitions(ms0515_files_tests PRIVATE
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FIXTURE_DISKS_DIR="${CMAKE_SOURCE_DIR}/lib/tests/disks"
)

target_link_libraries(ms0515_files_tests
    PRIVATE
        ms0515_files
        doctest::doctest
)

add_test(NAME ms0515_files_tests COMMAND ms0515_files_tests)
