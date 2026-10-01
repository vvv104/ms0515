# ── ms0515-run tests ─────────────────────────────────────────────────────────
# The packing, and the machine from the carried data to a program run and
# over - the calls main() makes, without a terminal or a window.  The
# programs run are made in the tests or are DEC's utilities of the RT-11
# development tree's system folder.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_run_tests
    test_main.cpp
    test_zero_run.cpp
    test_machine.cpp
)

target_compile_definitions(ms0515_run_tests PRIVATE
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    RT11_SYSTEM_DIR="${CMAKE_SOURCE_DIR}/../rt11_devel/toolset/system"
)

target_link_libraries(ms0515_run_tests
    PRIVATE
        ms0515_run_core
        doctest::doctest
)

add_test(NAME ms0515_run_tests COMMAND ms0515_run_tests)
