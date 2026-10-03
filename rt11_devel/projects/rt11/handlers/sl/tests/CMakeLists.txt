# ── SL tests ─────────────────────────────────────────────────────────────────
# The single-line editor's own harness: boots DEC's RT-11 for the machine -
# the dec system, composed here from the software collection - with SL.SYS
# on it, headlessly through ms0515_lib, and drives the command line from the
# keyboard: what the monitor ran and what the screen shows.  These are tests
# of SL, not of the emulator, which is why they live here and not under src/.
#
# The SL under test is the collection's (kits/dec/handlers/SL.SYS); a fresh
# build is named on the command line: sl_tests --sl-sys=<build>/work/sl.sys.
# Everything skips itself when there is none.  Without the collection
# ($MS0515_SOFTWARE_DIR, else the checkout beside this repository) there is
# no system to boot and the harness is left out.
# ─────────────────────────────────────────────────────────────────────────────

if(DEFINED ENV{MS0515_SOFTWARE_DIR})
    file(TO_CMAKE_PATH "$ENV{MS0515_SOFTWARE_DIR}" SL_SOFTWARE_DIR)
else()
    get_filename_component(SL_SOFTWARE_DIR
        "${CMAKE_SOURCE_DIR}/../../ms0515-software" ABSOLUTE)
endif()

if(NOT EXISTS "${SL_SOFTWARE_DIR}/kits/dec/bundles.toml" OR NOT TARGET ms0515-disk)
    message(STATUS "sl_tests: no dec kit at ${SL_SOFTWARE_DIR} "
                   "(set MS0515_SOFTWARE_DIR) - not built")
    return()
endif()

find_package(doctest REQUIRED)

# The diskette: the dec system and PIP, a program whose command line SL
# edits too.
set(SL_SYSTEM_DISK "${CMAKE_CURRENT_BINARY_DIR}/system.dsk")
add_custom_command(
    OUTPUT  "${SL_SYSTEM_DISK}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${SL_SYSTEM_DISK}"
    COMMAND ms0515-disk compose --repo "${SL_SOFTWARE_DIR}"
            --system dec --media ss --add pip-rt11 "${SL_SYSTEM_DISK}"
    DEPENDS ms0515-disk
            "${SL_SOFTWARE_DIR}/kits/dec/bundles.toml"
            "${SL_SOFTWARE_DIR}/kits/dec/RT11SJ.SYS"
            "${SL_SOFTWARE_DIR}/kits/dec/handlers/DZ.SYS"
            "${SL_SOFTWARE_DIR}/kits/dec/handlers/TT.SYS"
            "${SL_SOFTWARE_DIR}/kits/dec/utils/PIP.SAV"
    COMMENT "Composing the dec system diskette for sl_tests"
    VERBATIM
)
add_custom_target(sl_tests_system DEPENDS "${SL_SYSTEM_DISK}")

add_executable(sl_tests
    test_main.cpp
    test_console.cpp
    test_edit.cpp
    test_history.cpp
    test_program.cpp
)
add_dependencies(sl_tests sl_tests_system)

target_compile_definitions(sl_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    SL_SYSTEM_DISK="${SL_SYSTEM_DISK}"
    SL_SYS="${SL_SOFTWARE_DIR}/kits/dec/handlers/SL.SYS"
)

target_link_libraries(sl_tests
    PRIVATE
        ms0515_lib
        ms0515_disk
        doctest::doctest
)

add_test(NAME sl_tests COMMAND sl_tests)
