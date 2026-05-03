# ── ms0515_core unit tests ───────────────────────────────────────────────────
# Pure-core checks — these exercise only the C11 core (cpu, memory, timer,
# keyboard, ms7004, ramdisk, floppy) and never reach into the C++ wrapper.
# Linking only against ms0515_core (no ms0515_lib) is the structural test:
# if a core-tests source file ever needs lib symbols, that's a smell to
# resolve at the source rather than by adding a link dep here.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_core_tests
    test_main.cpp
    test_memory.cpp
    test_timer.cpp
    test_keyboard.cpp
    test_ms7004.cpp
    test_ramdisk.cpp
    test_floppy.cpp
)

target_compile_features(ms0515_core_tests PRIVATE c_std_11 cxx_std_23)

target_compile_definitions(ms0515_core_tests PRIVATE
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ms0515_core_tests
    PRIVATE
        ms0515_core
        doctest::doctest
)

add_test(NAME ms0515_core_tests COMMAND ms0515_core_tests)
