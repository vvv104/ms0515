# ── ms0515_platform_serial tests ─────────────────────────────────────────────
# What every host can check: a port that is not there fails with a reason,
# the list of ports comes.  On POSIX a pseudo-terminal pair stands in for a
# serial line, so bytes go both ways through a port opened by name; Windows
# has no such pair without a driver.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

set(SERIAL_TEST_SRC test_main.cpp test_serial.cpp)
if(NOT WIN32)
    list(APPEND SERIAL_TEST_SRC test_serial_pty.cpp)
endif()

add_executable(ms0515_platform_serial_tests ${SERIAL_TEST_SRC})

target_link_libraries(ms0515_platform_serial_tests
    PRIVATE
        ms0515_platform_serial
        doctest::doctest
)

add_test(NAME ms0515_platform_serial_tests COMMAND ms0515_platform_serial_tests)
