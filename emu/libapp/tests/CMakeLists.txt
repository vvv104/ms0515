# ── ms0515_libapp unit tests ─────────────────────────────────────────────────
# Test surface for the shared host-app code.  Paths / Config / Cli are
# pure (no emulator side); Disks goes through ms0515::Emulator for the
# mountDisksFromCli case, which is why this test exe links lib too —
# libapp already brings it as a public dependency.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_libapp_tests
    test_main.cpp
    test_paths.cpp
    test_config.cpp
    test_cli.cpp
    test_disks.cpp
)

target_compile_features(ms0515_libapp_tests PRIVATE cxx_std_23)

target_compile_definitions(ms0515_libapp_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ms0515_libapp_tests
    PRIVATE
        ms0515_libapp
        doctest::doctest
)

add_test(NAME ms0515_libapp_tests COMMAND ms0515_libapp_tests)
