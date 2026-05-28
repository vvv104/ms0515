# ── ms0515_disk ──────────────────────────────────────────────────────────────
# Host-side RT-11 / MS-0515 disk tooling: physical layouts, directory
# parsing, file extraction, and multi-source recovery.  Standalone C++23 —
# no emulation primitives, no core API.  Reads disk images offline; it does
# not run the machine.  See disk_recovery/ for the methodology this serves.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_disk STATIC
    src/Layout.cpp
    src/Directory.cpp
    src/Image.cpp
)

target_compile_features(ms0515_disk PUBLIC cxx_std_23)

target_include_directories(ms0515_disk
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
