# ── ms0515_disk unit tests ───────────────────────────────────────────────────
# Layer-local checks for the disk tooling.  Pure host-side logic — no
# emulator, no disk fixtures needed for the layout maths (the formulas
# are verified against the worked examples in docs/hardware/filesystem.md).
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_disk_tests
    test_main.cpp
    test_layout.cpp
    test_rtfs.cpp
    test_directory.cpp
    test_build.cpp
)

target_compile_features(ms0515_disk_tests PRIVATE cxx_std_23)

target_link_libraries(ms0515_disk_tests
    PRIVATE
        ms0515_disk
        doctest::doctest
)

add_test(NAME ms0515_disk_tests COMMAND ms0515_disk_tests)
