# ── ms0515-run ───────────────────────────────────────────────────────────────
# Runs one RT-11 program: `ms0515-run PROGRAM.SAV [its command line]`.  The
# ROM, the dec system diskette and a saved state of the booted monitor are
# compiled into the binary, so it needs no file but the program.
#
#   ms0515_run_base   what the build step and the tool share: the packing,
#                     the starter program
#   ms0515-run-bake   build step: boots the system and writes the packed
#                     ROM, diskette and state as a .cpp
#   ms0515_run_core   the machine made of that .cpp: starts a program from
#                     its folder and knows when it is over (unit-tested)
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_run_base STATIC
    src/ZeroRun.cpp
    src/Starter.cpp
)

target_include_directories(ms0515_run_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ms0515_run_base
    PUBLIC ms0515_lib
)

add_executable(ms0515-run-bake
    bake/main.cpp
)

target_link_libraries(ms0515-run-bake
    PRIVATE ms0515_run_base
    PRIVATE ms0515_disk
    PRIVATE fmt::fmt
)

# ── The system the state is made of ─────────────────────────────────────────
# The dec kit comes from the software collection: $MS0515_SOFTWARE_DIR, else
# the collection checked out next to this repository.  Without it there is
# nothing to bake and the tool is left out; the rest of the build goes on.
if(DEFINED ENV{MS0515_SOFTWARE_DIR})
    file(TO_CMAKE_PATH "$ENV{MS0515_SOFTWARE_DIR}" MS0515_SOFTWARE_DIR)
else()
    get_filename_component(MS0515_SOFTWARE_DIR
        "${CMAKE_SOURCE_DIR}/../../ms0515-software" ABSOLUTE)
endif()

if(NOT EXISTS "${MS0515_SOFTWARE_DIR}/kits/dec/bundles.toml")
    message(STATUS "ms0515-run: no dec kit at ${MS0515_SOFTWARE_DIR} "
                   "(set MS0515_SOFTWARE_DIR) - not built")
    return()
endif()

set(RUN_ROM       "${CMAKE_SOURCE_DIR}/assets/rom/ms0515-romb.rom")
set(RUN_SYSTEM    "${CMAKE_CURRENT_BINARY_DIR}/system.dsk")
set(RUN_EMBEDDED  "${CMAKE_CURRENT_BINARY_DIR}/Embedded.cpp")

# The diskette: the monitor, the swap file and the three handlers, nothing
# of the system's suggested utilities.
add_custom_command(
    OUTPUT  "${RUN_SYSTEM}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${RUN_SYSTEM}"
    COMMAND ms0515-disk compose --repo "${MS0515_SOFTWARE_DIR}"
            --system dec --media ss --add hd-rt11 "${RUN_SYSTEM}"
    DEPENDS ms0515-disk
            "${MS0515_SOFTWARE_DIR}/kits/dec/bundles.toml"
            "${MS0515_SOFTWARE_DIR}/kits/dec/RT11SJ.SYS"
            "${MS0515_SOFTWARE_DIR}/kits/dec/handlers/DZ.SYS"
            "${MS0515_SOFTWARE_DIR}/kits/dec/handlers/TT.SYS"
            "${MS0515_SOFTWARE_DIR}/kits/dec/handlers/HD.SYS"
    COMMENT "Composing the dec system diskette for ms0515-run"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${RUN_EMBEDDED}"
    COMMAND ms0515-run-bake "${RUN_ROM}" "${RUN_SYSTEM}" "${RUN_EMBEDDED}"
    DEPENDS ms0515-run-bake "${RUN_ROM}" "${RUN_SYSTEM}"
    COMMENT "Baking the ROM, the diskette and the state into ms0515-run"
    VERBATIM
)

# ── The machine ──────────────────────────────────────────────────────────────
add_library(ms0515_run_core STATIC
    src/ConsoleText.cpp
    src/Machine.cpp
    src/MonitorWatch.cpp
    "${RUN_EMBEDDED}"
)

target_link_libraries(ms0515_run_core
    PUBLIC ms0515_run_base
)

add_executable(ms0515-run
    src/main.cpp
)

target_link_libraries(ms0515-run
    PRIVATE ms0515_run_core
    PRIVATE ms0515_platform_cli
    PRIVATE ms0515_win7compat
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Package output ──────────────────────────────────────────────────────────
# Next to the other binaries, though it needs nothing of what lies there.
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/../package")
add_custom_command(TARGET ms0515-run POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515-run>" "${PACKAGE_DIR}/"
    COMMENT "Copying ms0515-run into ${PACKAGE_DIR}"
)
