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
    test_vram_write_callback.cpp
    test_vram_mirror.cpp
    test_fist_screen.cpp
    test_fist_game.cpp
    test_ex_disk.cpp
    test_hd_disk.cpp
    test_dir_vs_os.cpp
    test_disk_cli.cpp
)

# CLI integration tests need the path to the ms0515-disk binary at test time.
# Pass it as a generator-expression-resolved define so it always points to the
# current build's output.
add_dependencies(ms0515_lib_tests ms0515-disk)

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
    # FIST VRAM oracle (test_fist_screen.cpp): a built FIST.SAV (a gitignored
    # build artifact) and where to dump the captured VRAM.  The test skips
    # itself when FIST.SAV is absent.
    FIST_SAV_PATH="${CMAKE_SOURCE_DIR}/../rt11_devel/projects/fist/FIST.SAV"
    FIST_VRAM_OUT_PATH="${CMAKE_CURRENT_BINARY_DIR}/fist_vram.bin"
)
# Generator-expression in target_compile_definitions: the actual path is
# resolved per-config at build time.
target_compile_definitions(ms0515_lib_tests PRIVATE
    "MS0515_DISK_BIN=\"$<TARGET_FILE:ms0515-disk>\"")

target_link_libraries(ms0515_lib_tests
    PRIVATE
        ms0515_lib
        ms0515_disk
        doctest::doctest
)

add_test(NAME ms0515_lib_tests COMMAND ms0515_lib_tests)
