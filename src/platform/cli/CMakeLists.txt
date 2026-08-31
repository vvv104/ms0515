# ── ms0515_platform_cli ──────────────────────────────────────────────────────
# Console raw mode, non-blocking stdin, signal handling and UTF-8 codepage
# setup for the text-mode CLI binary.  Per-OS .cpp selected at configure
# time so neither <windows.h> nor <termios.h> leaks to the other platform.
# ─────────────────────────────────────────────────────────────────────────────

if(WIN32)
    set(PLATFORM_CLI_SRC Platform_win32.cpp)
else()
    set(PLATFORM_CLI_SRC Platform_unix.cpp)
endif()

add_library(ms0515_platform_cli STATIC ${PLATFORM_CLI_SRC})

target_compile_features(ms0515_platform_cli PUBLIC cxx_std_20)

target_include_directories(ms0515_platform_cli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
