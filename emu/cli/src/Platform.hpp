/*
 * Platform.hpp — host abstractions used by the CLI.
 *
 * Cross-platform shims behind a uniform interface.  Real implementations
 * live in Platform_win32.cpp (Windows console + Win32 signal handling)
 * and Platform_unix.cpp (POSIX termios + signal handling).
 */

#ifndef MS0515_CLI_PLATFORM_HPP
#define MS0515_CLI_PLATFORM_HPP

#include <cstddef>
#include <cstdint>

namespace ms0515::cli {

/* Make stdout / stderr emit UTF-8 multibyte sequences correctly on
 * the host console.  On Windows that means SetConsoleOutputCP(CP_UTF8);
 * on POSIX it's a no-op since UTF-8 is the default locale.  Call
 * before printing any non-ASCII text (e.g. the --help block, which
 * has em/en-dashes); setTerminalRawMode() also sets the codepage as
 * part of going raw, but the help / version paths exit before
 * reaching that. */
void enableUtf8Output();

/* Install a SIGINT handler that sets a global "quit requested" flag.
 * Returns true on success.  The flag is observable via shouldQuit().
 * Ctrl-C from the host is NOT routed here — it's passed through to
 * the guest as СУ/C; the CLI's own quit escape is Ctrl-] (handled at
 * the bridge layer via requestQuit()).  The OS-level signal handler
 * only fires for window-close / SIGTERM / etc. */
bool installInterruptHandler();

/* True after the user has hit the CLI quit hotkey OR the host signalled
 * a window-close / SIGTERM.  Polled by the main loop to exit cleanly. */
bool shouldQuit();

/* Set the quit flag from inside the bridge — used by the Ctrl-]
 * detection in StdioBridge::enqueueKoi8. */
void requestQuit();

/* Put the controlling terminal into raw-ish mode: no line buffering,
 * no host echo (so the guest can echo via .TTYOUT), but ISIG kept
 * enabled so Ctrl-C still raises SIGINT.  Idempotent — safe to call
 * twice.  Stores the previous state so restoreTerminal() can revert. */
bool setTerminalRawMode();

/* Revert the terminal to whatever state setTerminalRawMode() captured.
 * Called from main() on exit and from the SIGINT handler to leave the
 * user's shell in a sane state. */
void restoreTerminal();

/* Non-blocking read of up to `cap` bytes from stdin into `buf`.
 * Returns the number of bytes read (0 if no data is currently
 * available, never blocks).  On EOF returns 0 (callers can detect EOF
 * separately via isStdinEof()). */
size_t readStdinNonBlocking(uint8_t *buf, size_t cap);

/* True once stdin has reached EOF. */
bool isStdinEof();

/* Write a buffer to stdout.  Used by the .TTYOUT / .PRINT hooks.
 * Does NOT flush — callers must invoke flushStdout() at meaningful
 * boundaries (end of each .TTYOUT / .PRINT call) so each guest-side
 * I/O syscall flushes once instead of once per byte. */
void writeStdout(const char *data, size_t n);

/* Force any buffered stdout bytes out to the host terminal. */
void flushStdout();

}  /* namespace ms0515::cli */

#endif  /* MS0515_CLI_PLATFORM_HPP */
