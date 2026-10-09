# ── GCC toolset tests ────────────────────────────────────────────────────────
# The example programs of ../examples - built on the host by GCC for
# pdp11-aout and wrapped into .SAVs - run by the machine of ms0515-run,
# headlessly, and what they print checked: HELLO's line, ARITH's sums
# against the host's own arithmetic, CALC's count of primes.  These are
# tests of the toolset's runtime and of aout2sav, not of the emulator,
# which is why they live here and not under src/.
#
# The programs are the examples' build in ../examples/build/sav (see
# ../examples/CMakeLists.txt); another folder is named on the command line:
# gcc_tests --gcc-sav=<folder>.  Everything skips itself when there is
# none.  Without ms0515-run's machine (it needs the software collection)
# the harness is left out.
# ─────────────────────────────────────────────────────────────────────────────

if(NOT TARGET ms0515_run_core)
    message(STATUS "gcc_tests: no ms0515-run machine - not built")
    return()
endif()

find_package(doctest REQUIRED)

add_executable(gcc_tests
    test_main.cpp
    test_programs.cpp
)

target_include_directories(gcc_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(gcc_tests PRIVATE
    GCC_SAV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../examples/build/sav"
)

target_link_libraries(gcc_tests
    PRIVATE
        ms0515_run_core
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME gcc_tests COMMAND gcc_tests)
