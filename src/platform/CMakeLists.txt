# ── ms0515 platform layer ────────────────────────────────────────────────────
# Host-side abstractions kept out of cli/ and frontend/ proper so the binary
# sources don't pull <windows.h> / <commdlg.h> / <termios.h> directly.  Two
# sublibs because the CLI's needs (raw stdin, signal handling, UTF-8 console
# setup) and the GUI's needs (file dialogs, font discovery, GUI-subsystem
# console attach) have very little overlap and pulling SDL into the CLI link
# would just be dead weight.
# ─────────────────────────────────────────────────────────────────────────────

add_subdirectory(cli)
add_subdirectory(gui)
add_subdirectory(win7compat)
