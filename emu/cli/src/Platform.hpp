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

/* Install a SIGINT handler that sets a global "quit requested" flag.
 * Returns true on success.  The flag is observable via shouldQuit(). */
bool installInterruptHandler();

/* True after the user has hit Ctrl-C (or sent SIGINT). */
bool shouldQuit();

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

/* Write a buffer to stdout and flush.  Used by the .TTYOUT / .PRINT
 * hooks so we don't depend on libc's iostream-level flushing. */
void writeStdout(const char *data, size_t n);

}  /* namespace ms0515::cli */

#endif  /* MS0515_CLI_PLATFORM_HPP */
