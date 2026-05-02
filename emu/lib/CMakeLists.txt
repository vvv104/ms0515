# ── ms0515_lib ───────────────────────────────────────────────────────────────
# C++ wrapper layer between the pure-C core and the frontend / debugger.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_lib STATIC
    src/Emulator.cpp
    src/Debugger.cpp
    src/Disassembler.cpp
    src/GdbStub.cpp
    src/ScreenReader.cpp
    src/Terminal.cpp
)

if(WIN32)
    target_link_libraries(ms0515_lib PUBLIC ws2_32)
endif()

target_compile_features(ms0515_lib PUBLIC cxx_std_23)

target_include_directories(ms0515_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ms0515_lib
    PUBLIC ms0515_core
)
