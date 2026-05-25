# ── libapp ────────────────────────────────────────────────────────────────
# Host-side application utilities shared by the SDL/ImGui frontend and the
# text-mode CLI: filesystem paths (exeDir, search roots), YAML config
# loader / writer, and command-line argument parser.  Linking both
# binaries against this library means a flag added to ms0515.exe is
# automatically supported by ms0515-cli.exe — and vice versa.
#
# Strictly host-app code — no emulation primitives, no core API.
# ─────────────────────────────────────────────────────────────────────────────

add_library(ms0515_libapp STATIC
    src/Paths.cpp
    src/Config.cpp
    src/Cli.cpp
    src/Disks.cpp
)

target_compile_features(ms0515_libapp PUBLIC cxx_std_23)

target_include_directories(ms0515_libapp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Mounting helpers in Disks.cpp take an `ms0515::Emulator&`, so libapp
# depends on lib (which transitively brings core).
target_link_libraries(ms0515_libapp
    PUBLIC ms0515_lib
)
