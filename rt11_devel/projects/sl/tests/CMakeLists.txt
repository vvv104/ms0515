# ── SL completion tests ──────────────────────────────────────────────────────
# The harness of the SL with Tab: the machine of DEC's SL's tests
# (../../rt11/handlers/sl/tests/SlMachine.hpp) - the dec system composed
# from the software collection, booted headlessly through ms0515_lib - with
# this project's SL.SYS on it, and Tab pressed: the monitor's commands, their
# switches, SET's and SHOW's words, file names.
#
# The SL under test is the collection's (kits/dec/handlers/tab/SL.SYS); a
# fresh build is named on the command line:
# sltab_tests --sl-sys=<build>/work/sl.sys.  Everything skips itself when
# there is none.  Without the collection ($MS0515_SOFTWARE_DIR, else
# the checkout beside this repository) there is no system to boot and the
# harness is left out.
# ─────────────────────────────────────────────────────────────────────────────

if(DEFINED ENV{MS0515_SOFTWARE_DIR})
    file(TO_CMAKE_PATH "$ENV{MS0515_SOFTWARE_DIR}" SLTAB_SOFTWARE_DIR)
else()
    get_filename_component(SLTAB_SOFTWARE_DIR
        "${CMAKE_SOURCE_DIR}/../../ms0515-software" ABSOLUTE)
endif()

if(NOT EXISTS "${SLTAB_SOFTWARE_DIR}/kits/dec/bundles.toml" OR NOT TARGET ms0515-disk)
    message(STATUS "sltab_tests: no dec kit at ${SLTAB_SOFTWARE_DIR} "
                   "(set MS0515_SOFTWARE_DIR) - not built")
    return()
endif()

find_package(doctest REQUIRED)

# The diskette: the dec system with a few utilities and handlers - files to
# complete the names of, handlers with SET options, programs to run.
set(SLTAB_SYSTEM_DISK "${CMAKE_CURRENT_BINARY_DIR}/system.dsk")
add_custom_command(
    OUTPUT  "${SLTAB_SYSTEM_DISK}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${SLTAB_SYSTEM_DISK}"
    COMMAND ms0515-disk compose --repo "${SLTAB_SOFTWARE_DIR}"
            --system dec --media ss --add pip-rt11,dir-rt11,dup-rt11,resorc-rt11,dv-rt11,ld-rt11 "${SLTAB_SYSTEM_DISK}"
    DEPENDS ms0515-disk
            "${SLTAB_SOFTWARE_DIR}/kits/dec/bundles.toml"
            "${SLTAB_SOFTWARE_DIR}/kits/dec/RT11SJ.SYS"
            "${SLTAB_SOFTWARE_DIR}/kits/dec/handlers/DZ.SYS"
            "${SLTAB_SOFTWARE_DIR}/kits/dec/handlers/TT.SYS"
            "${SLTAB_SOFTWARE_DIR}/kits/dec/utils/PIP.SAV"
            "${SLTAB_SOFTWARE_DIR}/kits/dec/utils/DIR.SAV"
    COMMENT "Composing the dec system diskette for sltab_tests"
    VERBATIM
)
add_custom_target(sltab_tests_system DEPENDS "${SLTAB_SYSTEM_DISK}")

add_executable(sltab_tests
    ../../rt11/handlers/sl/tests/test_main.cpp
    test_overlay.cpp
    test_complete.cpp
    test_history.cpp
    test_switches.cpp
    test_files.cpp
    test_subcommands.cpp
)
add_dependencies(sltab_tests sltab_tests_system)

target_include_directories(sltab_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../rt11/handlers/sl/tests
)

target_compile_definitions(sltab_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    SL_SYSTEM_DISK="${SLTAB_SYSTEM_DISK}"
    SL_SYS="${SLTAB_SOFTWARE_DIR}/kits/dec/handlers/tab/SL.SYS"
)

target_link_libraries(sltab_tests
    PRIVATE
        ms0515_lib
        ms0515_disk
        doctest::doctest
)

add_test(NAME sltab_tests COMMAND sltab_tests)
