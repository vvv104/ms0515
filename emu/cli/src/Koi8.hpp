/*
 * Koi8.hpp — KOI-8R ↔ UTF-8 conversion.
 *
 * MS-0515 stores text in KOI-8R (Russian variant).  The host terminal
 * is set to UTF-8 in main() (CP_UTF8 on Windows, native on POSIX).
 * One byte of KOI-8 maps to 1–2 bytes of UTF-8; one UTF-8 codepoint
 * maps back to 1 byte of KOI-8 (unrepresentable code-points fall
 * back to '?').
 */

#ifndef MS0515_CLI_KOI8_HPP
#define MS0515_CLI_KOI8_HPP

#include <cstdint>
#include <string>

namespace ms0515::cli::koi8 {

/* Append the UTF-8 encoding of `b` (interpreted as KOI-8R) to `out`. */
void appendAsUtf8(std::string &out, uint8_t b);

/* Decode a single UTF-8 code-point from `data[0..size)`.  Returns the
 * number of bytes consumed and writes the matching KOI-8R byte to
 * `*out` (or '?' if the code-point isn't in KOI-8R).  Returns 0 if
 * the input is too short to hold a complete code-point. */
size_t utf8ToKoi8(const uint8_t *data, size_t size, uint8_t *out);

}  /* namespace ms0515::cli::koi8 */

#endif  /* MS0515_CLI_KOI8_HPP */
