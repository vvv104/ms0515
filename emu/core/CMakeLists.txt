# ── ms0515_core ──────────────────────────────────────────────────────────────
# Pure C11 emulation library.  No OS dependencies beyond <stdint.h>,
# <stdbool.h>, <string.h>, and <assert.h>.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_core STATIC
    src/cpu.c
    src/cpu_ops.c
    src/memory.c
    src/board.c
    src/timer.c
    src/keyboard.c
    src/ms7004.c
    src/floppy.c
    src/ramdisk.c
    src/trace.c
    src/snapshot.c
)

target_compile_features(ms0515_core PUBLIC c_std_11)

target_include_directories(ms0515_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
