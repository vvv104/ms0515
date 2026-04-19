# ── Unit tests ────────────────────────────────────────────────────────────────
# Uses doctest via Conan.  Each test_*.cpp file is compiled into a single
# test executable that links against ms0515_core and ms0515_lib.
# ──────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

add_executable(ms0515_tests
    test_main.cpp
    test_cpu.cpp
    test_disassembler.cpp
)

target_compile_features(ms0515_tests PRIVATE cxx_std_23)

target_link_libraries(ms0515_tests
    PRIVATE
        ms0515_lib
        ms0515_core
        doctest::doctest
)

add_test(NAME ms0515_tests COMMAND ms0515_tests)
