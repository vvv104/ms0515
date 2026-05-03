cmake_minimum_required(VERSION 3.16)
project(ms0515 VERSION 0.3.0 LANGUAGES C CXX)

# ── Build options ────────────────────────────────────────────────────────────
option(MS0515_BUILD_FRONTEND "Build the SDL2/ImGui frontend"  ON)
option(MS0515_BUILD_TESTS    "Build unit tests"               OFF)

# ── Compiler warnings ───────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(MS0515_BUILD_TESTS)
    enable_testing()
endif()

# ── Sub-projects ─────────────────────────────────────────────────────────────
# Each layer pulls in its own tests/ subdirectory when MS0515_BUILD_TESTS is
# on, so each yields a separate test binary.  The top-level tests/ folder
# is gone — keeping the test split aligned with the layering enforces (and
# documents) the dependency direction at link time.
add_subdirectory(core)
add_subdirectory(lib)

if(MS0515_BUILD_FRONTEND)
    add_subdirectory(frontend)
endif()
