# ── ms0515_platform_gui ──────────────────────────────────────────────────────
# Native file dialogs, font discovery, console attach (for `ms0515 --help`)
# and the SDL_syswm bridge the dialog code uses to plant the picker against
# the SDL window.  SDL2 is a public dependency because Platform.hpp takes
# `SDL_Window *` parameters; ImGui isn't needed here.
# ─────────────────────────────────────────────────────────────────────────────

find_package(SDL2 REQUIRED)

if(WIN32)
    set(PLATFORM_GUI_SRC Platform_win32.cpp)
else()
    set(PLATFORM_GUI_SRC Platform_unix.cpp)
endif()

add_library(ms0515_platform_gui STATIC ${PLATFORM_GUI_SRC})

target_include_directories(ms0515_platform_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ms0515_platform_gui PUBLIC SDL2::SDL2)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
