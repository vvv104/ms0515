# ── MANICM port tests ────────────────────────────────────────────────────────
# The game's own test harness: boots the built MANICM.SAV / MANICM.DAT
# headlessly through ms0515_lib (the emulator's public API) and checks the
# port from the outside - the title, the keys, the demo, the cheat - reading
# the game's state where the link map (MANICM.MAP) puts it.  These are tests
# of the GAME, not of the emulator, which is why they live here and not
# under src/.  Everything skips itself when the game is not built (its data
# file is the original's and is never committed).
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(manicm_tests
    test_main.cpp
    test_game.cpp
)

target_include_directories(manicm_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(manicm_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    MANICM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    MANICM_SYSTEM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../toolset/system"
)

target_link_libraries(manicm_tests
    PRIVATE
        ms0515_lib
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME manicm_tests COMMAND manicm_tests)
