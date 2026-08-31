cmake_minimum_required(VERSION 3.16)
# Single source of truth for the version: src/VERSION (also read by
# conanfile.py).  One file to bump, never two literals to keep in sync.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MS0515_VERSION)
project(ms0515 VERSION ${MS0515_VERSION} LANGUAGES C CXX)

# ── Language standard ───────────────────────────────────────────────────────
# Declared once for the whole project: every target created below inherits
# it, including the ones in rt11_devel/ and any added later.  A target that
# genuinely needs something else says so itself with target_compile_features;
# nothing else should repeat these.
set(CMAKE_C_STANDARD              11)
set(CMAKE_C_STANDARD_REQUIRED     ON)
set(CMAKE_CXX_STANDARD            20)
set(CMAKE_CXX_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_EXTENSIONS          OFF)

# ── Version macro for --version flag ─────────────────────────────────────────
# Push the CMake project version through to C++ so both binaries can echo it
# without anyone having to bump a stand-alone string literal.
add_compile_definitions(MS0515_VERSION_STRING="${PROJECT_VERSION}")

# ── Build options ────────────────────────────────────────────────────────────
option(MS0515_BUILD_FRONTEND "Build the SDL2/ImGui frontend"  ON)
option(MS0515_BUILD_TESTS    "Build unit tests"               OFF)

# ── Compiler warnings ───────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Windows API floor ────────────────────────────────────────────────────────
# Target Windows 7 SP1 as the minimum supported OS so the released exes run
# on Win7/8/8.1/10/11.  The Windows SDK guards Win8+ symbols behind these
# version macros; calling something that requires a newer floor turns into a
# compile error here, not a "entry point not found in DLL" at runtime on Win7.
# Combined with the static MSVC runtime (configured in CI), the resulting
# binaries don't need VCRedist on the target machine either.
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601     # Windows 7
        WINVER=0x0601           # legacy alias for the same
        NTDDI_VERSION=0x06010000 # Windows 7 (DDI versioning)
    )
endif()

if(MS0515_BUILD_TESTS)
    enable_testing()
endif()

# ── Formatting ──────────────────────────────────────────────────────────────
# {fmt} replaces std::format: the project stays on C++20 so it can be built
# with the stock compiler on older macOS, whose libc++ has no <format>.
find_package(fmt REQUIRED)

# ── Sub-projects ─────────────────────────────────────────────────────────────
# Each layer pulls in its own tests/ subdirectory when MS0515_BUILD_TESTS is
# on, so each yields a separate test binary.  The top-level tests/ folder
# is gone — keeping the test split aligned with the layering enforces (and
# documents) the dependency direction at link time.
# The disk library reports through exceptions and the web module catches
# them; Emscripten leaves C++ exceptions off by default (a throw would then
# abort, a bare pointer number reaching the page), so the whole build has
# them on there.
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions)
endif()

add_subdirectory(core)
add_subdirectory(lib)
add_subdirectory(disk)

# The browser build (the Emscripten toolchain, src/profiles/emscripten)
# has no host side: the core, the lib and the web front-end only.
if(EMSCRIPTEN)
    add_subdirectory(web)
else()
    add_subdirectory(libapp)
    add_subdirectory(platform)
    add_subdirectory(cli)
    add_subdirectory(tools)
    if(MS0515_BUILD_FRONTEND)
        add_subdirectory(frontend)
    endif()
endif()

# The RT-11 development tree's program harnesses (rt11_devel/projects/*/tests)
# test built guest programs through the lib - they are tests of those
# programs, so they live with them, not under src/.
if(MS0515_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/../rt11_devel/CMakeLists.txt")
    add_subdirectory("${CMAKE_SOURCE_DIR}/../rt11_devel" "${CMAKE_BINARY_DIR}/rt11_devel")
endif()
