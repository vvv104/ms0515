/*
 * Viewer.hpp — a file's bytes as lines of text: plain text in one of the
 * machine's encodings, an octal dump, a hex dump.  The same choices the
 * web commander's viewer offers (F1 the representation, F4 the encoding,
 * F2 wrap), so the two look alike.  Pure functions over a byte span; the
 * terminal front-end scrolls through the lines.
 */
#ifndef MS0515_FILES_VIEWER_HPP
#define MS0515_FILES_VIEWER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ms0515::files {

enum class View { text, octal, hex };

/* The machine's encodings: 7-bit ASCII (high bit shown as '.'); KOI-8R
 * (the .COM files, most program text); KOI-7 (the games: the lower-case
 * Latin positions ARE the Cyrillic letters); KOI-7 with the ^N / ^O
 * (SO / SI) shifts switching РУС / ЛАТ, as the terminal driver does; and
 * CP866 (the manuals and application data). */
enum class Encoding { ascii, koi8r, koi7, koi7shift, cp866 };

struct ViewOptions {
    View     view     = View::text;
    Encoding encoding = Encoding::koi8r;
    bool     wrap     = true;       /* text: break lines at the machine's 80 columns */
};

/* The lines to show.  Text: CR/LF-separated lines, tabs expanded, controls
 * as '.'; octal: "000000: 000000 000000 ..." eight words a line, as the
 * DUMP utility prints; hex: "000000  00 01 ...  |ascii|" sixteen bytes a
 * line. */
[[nodiscard]] std::vector<std::string> renderLines(std::span<const uint8_t> bytes, const ViewOptions &opts);

/* A text file, as RT-11 keeps them: printable bytes, the line ends, tabs,
 * form feeds, the KOI-7 shifts, a Ctrl-Z, then the last block's NUL
 * padding - and nothing else.  Anything with a control byte elsewhere,
 * a NUL inside, is binary and opens in the hex dump. */
[[nodiscard]] bool isTextLike(std::span<const uint8_t> bytes);
/* The text without its end: the block padding of NULs and a Ctrl-Z. */
[[nodiscard]] std::span<const uint8_t> textBody(std::span<const uint8_t> bytes);

/* One byte of the file to UTF-8 in `encoding` (for koi7shift pass the
 * current shift and let the function update it). */
[[nodiscard]] std::string decodeByte(uint8_t byte, Encoding encoding, bool &rusShift);

/* A string typed by the user, encoded the way the file is, so it can be
 * searched for.  nullopt for a character the encoding has not. */
[[nodiscard]] std::optional<std::vector<uint8_t>> encodeString(const std::string &utf8, Encoding encoding);

/* The offset of `needle` in `bytes` at or after `from`. */
[[nodiscard]] std::optional<size_t> findBytes(std::span<const uint8_t> bytes, std::span<const uint8_t> needle, size_t from);

/* The names the front-end shows in its key bar. */
[[nodiscard]] const char *viewName(View view);
[[nodiscard]] const char *encodingName(Encoding encoding);
[[nodiscard]] View nextView(View view);
[[nodiscard]] Encoding nextEncoding(Encoding encoding);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_VIEWER_HPP */
