# ── ms0515_lib unit tests ────────────────────────────────────────────────────
# Lib-level checks — these go through the C++ wrapper (Emulator,
# ScreenReader, Terminal, Disassembler, Debugger).  Linking against
# ms0515_lib pulls ms0515_core transitively, but tests should not include
# raw <ms0515/*.h> headers directly: doing so would defeat the layering and
# reintroduce the leak the lib API revision is trying to plug.  Once the
# Emulator.hpp PIMPL lands (Phase A), the build will enforce that on its
# own — until then, treat any new core-include in this directory as a
# regression that needs a lib-side accessor instead.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_lib_tests
    test_main.cpp
    test_cpu.cpp
    test_board.cpp
    test_boot.cpp
    test_disassembler.cpp
    test_keyboard_emulated.cpp
    test_screen_reader.cpp
    test_terminal.cpp
)

target_compile_features(ms0515_lib_tests PRIVATE cxx_std_23)

target_compile_definitions(ms0515_lib_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    TESTS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ms0515_lib_tests
    PRIVATE
        ms0515_lib
        doctest::doctest
)

add_test(NAME ms0515_lib_tests COMMAND ms0515_lib_tests)
