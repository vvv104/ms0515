# ── FIST port tests ──────────────────────────────────────────────────────────
# The game's own test harness: boots the built FIST.SAV / GST.DAT headlessly
# through ms0515_lib (the emulator's public API) and checks the port from the
# outside - pixel-exact renders, the match logic's state, the keyboard map.
# These are tests of the GAME, not of the emulator, which is why they live
# here and not under src/.  They link the lib and, like the lib's own tests,
# may reach into lib/src/EmulatorInternal.hpp for the board/cpu accessors.
# Everything skips itself when the game is not built (its artifacts embed the
# original's art and are never committed).
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(fist_tests
    test_main.cpp
    test_oracle.cpp
    test_game.cpp
    test_diag.cpp
)

target_compile_features(fist_tests PRIVATE cxx_std_23)

target_include_directories(fist_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(fist_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    FIST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    FIST_SYSTEM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../toolset/system"
    FIST_DSK="${CMAKE_SOURCE_DIR}/../package/assets/disks/fist_game.dsk"
    FIST_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(fist_tests
    PRIVATE
        ms0515_lib
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME fist_tests COMMAND fist_tests)
