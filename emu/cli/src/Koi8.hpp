/*
 * Koi8.hpp — UTF-8 → KOI-8R conversion for host stdin.
 *
 * The host terminal is set to UTF-8 in main() (CP_UTF8 on Windows,
 * native on POSIX) but the guest kernel reads KOI-8R bytes through
 * the emulated MS-7004 keyboard.  This helper decodes incoming
 * code-points into the matching KOI-8R byte before the bridge feeds
 * them as keystrokes.
 *
 * Guest output goes the other way — VRAM → Terminal → stdout — and
 * is handled by Terminal::setOutput; there's no KOI-8 → UTF-8 path
 * on this side anymore.
 */

#ifndef MS0515_CLI_KOI8_HPP
#define MS0515_CLI_KOI8_HPP

#include <cstdint>
#include <cstddef>

namespace ms0515::cli::koi8 {

/* Decode a single UTF-8 code-point from `data[0..size)`.  Returns the
 * number of bytes consumed and writes the matching KOI-8R byte to
 * `*out` (or '?' if the code-point isn't in KOI-8R).  Returns 0 if
 * the input is too short to hold a complete code-point. */
size_t utf8ToKoi8(const uint8_t *data, size_t size, uint8_t *out);

}  /* namespace ms0515::cli::koi8 */

#endif  /* MS0515_CLI_KOI8_HPP */
