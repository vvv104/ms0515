# ── ms0515_lib unit tests ────────────────────────────────────────────────────
# Lib-level checks — they go through the C++ wrapper (Emulator,
# Terminal, KeyboardLayout, Disassembler, Debugger).  Linking against
# ms0515_lib pulls ms0515_core transitively, and these tests are
# allowed to peek at raw core symbols (`board_get_vram`,
# `kbd_push_scancode`, etc.) for white-box assertions — they live
# inside the lib layer.  Frontend code never gets that visibility:
# `<ms0515/Emulator.hpp>` is PIMPL'd, so the structural seam holds
# automatically.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_lib_tests
    test_main.cpp
    test_cpu.cpp
    test_board.cpp
    test_boot.cpp
    test_disassembler.cpp
    test_keyboard_emulated.cpp
    test_keyboard_layout.cpp
    test_terminal.cpp
)

target_compile_features(ms0515_lib_tests PRIVATE cxx_std_23)

# Lib tests live inside the lib layer, so they are allowed to reach
# into `lib/src/EmulatorInternal.hpp` for the typed `internal::cpu()` /
# `internal::board()` accessors.  Frontend code never gets this
# include path — that's the structural seam the API revision keeps.
target_include_directories(ms0515_lib_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

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
