/*
 * Platform.hpp — host abstractions used by the CLI.
 *
 * Cross-platform shims behind a uniform interface.  Real implementations
 * live in Platform_win32.cpp (Windows console + Win32 signal handling)
 * and Platform_unix.cpp (POSIX termios + signal handling).
 */

#ifndef MS0515_CLI_PLATFORM_HPP
#define MS0515_CLI_PLATFORM_HPP

namespace ms0515::cli {

/* Install a SIGINT handler that sets a global "quit requested" flag.
 * Returns true on success.  The flag is observable via shouldQuit(). */
bool installInterruptHandler();

/* True after the user has hit Ctrl-C (or sent SIGINT). */
bool shouldQuit();

}  /* namespace ms0515::cli */

#endif  /* MS0515_CLI_PLATFORM_HPP */
