# ── ms0515_platform_serial ───────────────────────────────────────────────────
# A host serial port, opened raw at a given speed, 8N1, non-blocking - for
# a device on one of the machine's serial lines (the keyboard's).  Per-OS
# .cpp selected at configure time, as the other platform sublibs do.
# ─────────────────────────────────────────────────────────────────────────────

if(WIN32)
    add_library(ms0515_platform_serial STATIC Serial_win32.cpp)
    target_link_libraries(ms0515_platform_serial PRIVATE advapi32)
else()
    add_library(ms0515_platform_serial STATIC Serial_unix.cpp)
endif()

target_include_directories(ms0515_platform_serial PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
