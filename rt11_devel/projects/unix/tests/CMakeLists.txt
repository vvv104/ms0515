# The system's own tests: the diskette image the project builds
# (rt11_devel/projects/unix/build/unix.dsk) booted through ms0515_lib on the
# native ROM-B, the screen read as text.  Tests of the system, not of the
# emulator; they skip themselves when the image is not built, and fail when
# --unix-dsk=<file> names one that is missing.

find_package(doctest REQUIRED)

add_executable(unix_tests
    test_main.cpp
    test_boot.cpp
    test_console.cpp
    test_fs.cpp
    test_shell.cpp
    test_swap.cpp
    test_write.cpp
    test_mount.cpp
    test_rc.cpp
    test_cache.cpp
    test_sides.cpp
    test_driver.cpp
    test_kstack.cpp
)

# Machine.hpp reads the RAM disk's memory through the lib's internal access
# to the board, as the FIST tests do.
target_include_directories(unix_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)

target_compile_definitions(unix_tests PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    UNIX_DSK="${CMAKE_CURRENT_SOURCE_DIR}/../build/unix.dsk"
    UNIX_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../root"
)

target_link_libraries(unix_tests
    PRIVATE
        ms0515_lib
        ms0515_disk          # EmulatorInternal.hpp reaches the disk lib's headers
        doctest::doctest
)

add_test(NAME unix_tests COMMAND unix_tests)
