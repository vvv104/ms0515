# ── ms0515_lib ───────────────────────────────────────────────────────────────
# C++ wrapper layer between the pure-C core and the frontend / debugger.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_lib STATIC
    src/Emulator.cpp
    src/Debugger.cpp
    src/Disassembler.cpp
    src/GdbStub.cpp
    src/KeyboardLayout.cpp
    src/Terminal.cpp
    src/VramMirror.cpp
)

if(WIN32)
    target_link_libraries(ms0515_lib PUBLIC ws2_32)
endif()

target_include_directories(ms0515_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ms0515_lib
    PUBLIC ms0515_core
    # Folder-backed HD volumes (.rtfs) are served by the offline disk lib.
    PRIVATE ms0515_disk
    PRIVATE fmt::fmt
)

if(MS0515_BUILD_TESTS)
    add_subdirectory(tests)
endif()
