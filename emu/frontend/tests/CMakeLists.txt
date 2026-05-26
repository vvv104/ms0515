# ── ms0515 frontend unit tests ───────────────────────────────────────────────
# Currently exercises the Platform abstraction layer only — anything that
# would need SDL2 / ImGui at runtime stays out, since the tests run head-
# less in CI and the goal is to keep them fast.
# ─────────────────────────────────────────────────────────────────────────────

find_package(doctest REQUIRED)

# Platform_*.cpp pulls in SDL_syswm / commdlg / windows.h on Win32; rather
# than mock those, link the same per-OS file the frontend uses.  SDL2 stays
# on the link line for the few headers Platform_*.cpp drags in.
if(WIN32)
    set(PLATFORM_SRC ../src/Platform_win32.cpp)
else()
    set(PLATFORM_SRC ../src/Platform_unix.cpp)
endif()

add_executable(ms0515_frontend_tests
    test_main.cpp
    test_platform.cpp
    ${PLATFORM_SRC}
)

target_compile_features(ms0515_frontend_tests PRIVATE cxx_std_23)

target_include_directories(ms0515_frontend_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

find_package(SDL2 REQUIRED)
target_link_libraries(ms0515_frontend_tests
    PRIVATE doctest::doctest
    PRIVATE SDL2::SDL2 SDL2::SDL2main
)

add_test(NAME ms0515_frontend_tests COMMAND ms0515_frontend_tests)
