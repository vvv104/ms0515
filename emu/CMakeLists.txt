cmake_minimum_required(VERSION 3.16)
project(ms0515 VERSION 0.1.0 LANGUAGES C CXX)

# ── Build options ────────────────────────────────────────────────────────────
option(MS0515_BUILD_FRONTEND "Build the SDL2/ImGui frontend"  ON)
option(MS0515_BUILD_TESTS    "Build unit tests"               OFF)

# ── Compiler warnings ───────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Sub-projects ─────────────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(lib)

if(MS0515_BUILD_FRONTEND)
    add_subdirectory(frontend)
endif()

if(MS0515_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
