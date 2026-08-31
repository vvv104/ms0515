# ── ms0515_win7compat ────────────────────────────────────────────────────────
# Win7-SP1 API shims for the static MSVC runtime.  Linked into every
# released exe so the resulting binaries run on Win7/8/8.1/10/11 without
# tripping over CreateFile2 (Win8+) that the UCRT pulls in by default.
# On non-MSVC builds the library is an INTERFACE no-op so dependents keep
# the same link line on every platform.
# ─────────────────────────────────────────────────────────────────────────────

if(MSVC)
    add_library(ms0515_win7compat STATIC win7_shims.cpp)
    target_compile_features(ms0515_win7compat PUBLIC cxx_std_20)
else()
    add_library(ms0515_win7compat INTERFACE)
endif()
