# ── Minesweeper tests ────────────────────────────────────────────────────────
# The game's own harness: MINE.SAV started from its folder by the machine of
# ms0515-run, headlessly, and played - the keys pressed, the sprites read
# out of video memory, the field out of the program's.  These are tests of
# the game, not of the emulator, which is why they live here and not under
# src/.
#
# The game is the project's build in rt11_devel/projects/minesweeper/build
# (see ../CMakeLists.txt); another folder is named on the command line:
# mine_tests --mine-game=<folder>.  Everything skips itself when there is
# none.  Without ms0515-run's machine (it needs the software collection)
# the harness is left out.
# ─────────────────────────────────────────────────────────────────────────────

if(NOT TARGET ms0515_run_core)
    message(STATUS "mine_tests: no ms0515-run machine - not built")
    return()
endif()

find_package(doctest REQUIRED)

add_executable(mine_tests
    test_main.cpp
    test_game.cpp
)

target_include_directories(mine_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(mine_tests PRIVATE
    MINE_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../build/game"
    MINE_SPRITES="${CMAKE_CURRENT_SOURCE_DIR}/../MINE.DAT"
)

target_link_libraries(mine_tests
    PRIVATE
        ms0515_run_core
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME mine_tests COMMAND mine_tests)
