# ── SAB2TUNE tests ───────────────────────────────────────────────────────────
# The tune's own harness: SABTUN.SAV booted headlessly through ms0515_lib
# with the speaker recorded, its level changes compared one by one with
# the model's rendering of the original (SAB2TN.REF, written by the build
# from the tape) - the pitch, the timbre, the drums and the durations of
# the whole tune.  A test of the PORT, not of the emulator, hence here.
# Everything skips itself when the tune is not built (the tape is the
# original's and never committed).
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(sab2tune_tests
    test_main.cpp
    test_tune.cpp
    test_diag.cpp
    test_trace.cpp
)

target_include_directories(sab2tune_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(sab2tune_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    SAB2TUNE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    SAB2TUNE_SYSTEM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../toolset/system"
)

target_link_libraries(sab2tune_tests
    PRIVATE
        ms0515_lib
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME sab2tune_tests COMMAND sab2tune_tests)
