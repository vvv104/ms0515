# ── STARS tests ──────────────────────────────────────────────────────────────
# The program's own harness: STARS.SAV flown on the machine of ms0515-run,
# headlessly - the screen read out of video memory, the keys pressed.
# These are tests of the program, not of the emulator, which is why they
# live here and not under src/.
#
# The program is the project's build in rt11_devel/projects/stars/build
# (see ../CMakeLists.txt); another folder is named on the command line:
# stars_tests --stars-sav=<folder>.  Everything skips itself when there is
# none.  Without ms0515-run's machine (it needs the software collection)
# the harness is left out.
# ─────────────────────────────────────────────────────────────────────────────

if(NOT TARGET ms0515_run_core)
    message(STATUS "stars_tests: no ms0515-run machine - not built")
    return()
endif()

find_package(doctest REQUIRED)

add_executable(stars_tests
    test_main.cpp
    test_stars.cpp
)

target_include_directories(stars_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(stars_tests PRIVATE
    STARS_SAV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../build/sav"
)

target_link_libraries(stars_tests
    PRIVATE
        ms0515_run_core
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME stars_tests COMMAND stars_tests)
