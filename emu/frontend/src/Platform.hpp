/*
 * Platform.hpp — platform abstraction for the MS0515 frontend.
 *
 * Declares platform-specific operations.  Each platform provides its
 * own implementation file (Platform_win32.cpp, Platform_unix.cpp),
 * selected at build time by CMake.
 */

#ifndef MS0515_FRONTEND_PLATFORM_HPP
#define MS0515_FRONTEND_PLATFORM_HPP

#include <string>
#include <vector>

struct SDL_Window;

namespace ms0515_frontend {

/* One-time platform init (e.g. console codepage on Windows).
 * Call early in main(), before any output. */
void platformInit();

/* Open a native file-picker dialog.  Returns the chosen path,
 * or an empty string if cancelled. */
std::string openFileDialog(SDL_Window *owner, const char *title);

/* Open a native save-file dialog.  Returns the chosen path,
 * or an empty string if cancelled. */
std::string saveFileDialog(SDL_Window *owner, const char *title,
                           const char *defaultName);

/* System font paths for Cyrillic+Latin rendering. */
std::vector<std::string> systemFontCandidates();

/* System font paths for symbol/arrow glyph fallback. */
std::vector<std::string> symbolFontCandidates();

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_PLATFORM_HPP */
