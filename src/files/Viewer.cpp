/*
 * Viewer.cpp — bytes to lines in the machine's encodings, octal, hex.
 */
#include "Viewer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace ms0515::files {

namespace {

/* KOI-7 N1: the 0x60..0x7E positions (lower-case Latin) are the Cyrillic
 * letters - Ю А Б Ц Д Е Ф Г Х И Й К Л М Н О П Я Р С Т У Ж В Ь Ы З Ш Э Щ Ч. */
constexpr std::array<const char *, 31> kKoi7 = {
    "Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й", "К", "Л", "М", "Н", "О",
    "П", "Я", "Р", "С", "Т", "У", "Ж", "В", "Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч"};

/* KOI-8R 0xC0..0xFF: the lower-case row, then the upper-case row, in the
 * same order as KOI-7. */
constexpr std::array<const char *, 32> kKoi8Lower = {
    "ю", "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й", "к", "л", "м", "н", "о",
    "п", "я", "р", "с", "т", "у", "ж", "в", "ь", "ы", "з", "ш", "э", "щ", "ч", "ъ"};
constexpr std::array<const char *, 32> kKoi8Upper = {
    "Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й", "К", "Л", "М", "Н", "О",
    "П", "Я", "Р", "С", "Т", "У", "Ж", "В", "Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "Ъ"};
/* 0x80..0xBF (octal 200-277), the pseudographics - the machine's own, not
 * KOI-8R's.  ROM-B draws them from its table of 64 glyphs at 157000: the
 * mixed single/double lines, the double lines and the shades, the single
 * lines and the blocks, then Ё ё, four diagonal quarters (the round
 * corners), the arrows and signs, and a blank.  ROM-A prints nothing for
 * them.  Rodionov's monitor draws them on ROM-A from a table of its own:
 * the single and the mixed rows swapped, his signs in the last row, 233
 * not printed (the 8-bit CSI) and 274 a second corner. */
constexpr std::array<const char *, 64> kRomBGraph = {
    "╧", "╨", "╤", "╡", "╢", "╖", "╕", "╥", "╙", "╘", "╒", "╜", "╛", "╞", "╟", "╓",
    "╔", "╗", "╝", "╚", "═", "║", "╦", "╣", "╩", "╠", "╬", "░", "▒", "▓", "╫", "╪",
    "┌", "┐", "┘", "└", "─", "│", "┬", "┤", "┴", "├", "┼", "█", "▄", "▌", "▐", "▀",
    "Ё", "ё", "╭", "╮", "╯", "╰", "→", "←", "↑", "↓", "÷", "±", "№", "¤", "■", " "};
constexpr std::array<const char *, 64> kRodGraph = {
    "┌", "┐", "┘", "└", "─", "│", "┬", "┤", "┴", "├", "┼", "█", "▄", "▌", "▐", "▀",
    "╔", "╗", "╝", "╚", "═", "║", "╦", "╣", "╩", "╠", "╬", ".", "▒", "▓", "╫", "╪",
    "╧", "╨", "╤", "╡", "╢", "╖", "╕", "╥", "╙", "╘", "╒", "╜", "╛", "╞", "╟", "╓",
    "°", "ё", "►", "◄", "▲", "▼", "→", "←", "↓", "↑", "÷", "░", "┌", "±", "№", "©"};

/* The four ends of a line-drawing character - up, right, down, left - as
 * 0 none, 1 single, 2 double (two bits each, up lowest); 0 for the rest. */
unsigned edgesOf(const std::string &ch)
{
    struct Edge { const char *ch; unsigned u, r, d, l; };
    static constexpr Edge kEdges[] = {
        {"─", 0, 1, 0, 1}, {"│", 1, 0, 1, 0}, {"┌", 0, 1, 1, 0}, {"┐", 0, 0, 1, 1}, {"└", 1, 1, 0, 0},
        {"┘", 1, 0, 0, 1}, {"├", 1, 1, 1, 0}, {"┤", 1, 0, 1, 1}, {"┬", 0, 1, 1, 1}, {"┴", 1, 1, 0, 1},
        {"┼", 1, 1, 1, 1}, {"═", 0, 2, 0, 2}, {"║", 2, 0, 2, 0}, {"╔", 0, 2, 2, 0}, {"╗", 0, 0, 2, 2},
        {"╚", 2, 2, 0, 0}, {"╝", 2, 0, 0, 2}, {"╠", 2, 2, 2, 0}, {"╣", 2, 0, 2, 2}, {"╦", 0, 2, 2, 2},
        {"╩", 2, 2, 0, 2}, {"╬", 2, 2, 2, 2}, {"╒", 0, 2, 1, 0}, {"╓", 0, 1, 2, 0}, {"╕", 0, 0, 1, 2},
        {"╖", 0, 0, 2, 1}, {"╘", 1, 2, 0, 0}, {"╙", 2, 1, 0, 0}, {"╛", 1, 0, 0, 2}, {"╜", 2, 0, 0, 1},
        {"╞", 1, 2, 1, 0}, {"╟", 2, 1, 2, 0}, {"╡", 1, 0, 1, 2}, {"╢", 2, 0, 2, 1}, {"╤", 0, 2, 1, 2},
        {"╥", 0, 1, 2, 1}, {"╧", 1, 2, 0, 2}, {"╨", 2, 1, 0, 1}, {"╪", 1, 2, 1, 2}, {"╫", 2, 1, 2, 1},
        {"╭", 0, 1, 1, 0}, {"╮", 0, 0, 1, 1}, {"╯", 1, 0, 0, 1}, {"╰", 1, 1, 0, 0}};
    for (const Edge &e : kEdges)
        if (ch == e.ch) return e.u | e.r << 2 | e.d << 4 | e.l << 6;
    return 0;
}

/* How well a table's lines join in the text: +1 for each pair of
 * neighbours (side by side, or one above the other) whose facing ends
 * meet in the same style, -1 where one end reaches out and the other
 * does not answer. */
template <typename GlyphOf>
int joins(std::span<const uint8_t> bytes, GlyphOf glyphOf)
{
    auto edges = [&](uint8_t b) { const char *g = glyphOf(b); return g ? edgesOf(g) : 0u; };
    auto score = [](unsigned out, unsigned in) { return out && in == out ? 1 : (out || in) ? -1 : 0; };
    std::vector<std::vector<unsigned>> rows(1);
    for (const uint8_t b : bytes) {
        if (b == '\n') rows.emplace_back();
        else if (b != '\r') rows.back().push_back(edges(b));
    }
    int total = 0;
    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < rows[y].size(); ++x) {
            const unsigned e = rows[y][x];
            if (x + 1 < rows[y].size() && (e || rows[y][x + 1]))
                total += score((e >> 2) & 3, (rows[y][x + 1] >> 6) & 3);
            if (y + 1 < rows.size() && x < rows[y + 1].size() && (e || rows[y + 1][x]))
                total += score((e >> 4) & 3, rows[y + 1][x] & 3);
        }
    }
    return total;
}

/* Pairs of different letters side by side: words, which frames are not
 * (a line of one byte repeated is no word in either encoding). */
template <typename IsLetter>
size_t letterPairs(std::span<const uint8_t> bytes, IsLetter isLetter)
{
    size_t n = 0;
    for (size_t i = 0; i + 1 < bytes.size(); ++i)
        if (bytes[i] != bytes[i + 1] && isLetter(bytes[i]) && isLetter(bytes[i + 1])) ++n;
    return n;
}

/* CP866 0x80..0xFF. */
constexpr std::array<const char *, 128> kCp866 = {
    "А", "Б", "В", "Г", "Д", "Е", "Ж", "З", "И", "Й", "К", "Л", "М", "Н", "О", "П",
    "Р", "С", "Т", "У", "Ф", "Х", "Ц", "Ч", "Ш", "Щ", "Ъ", "Ы", "Ь", "Э", "Ю", "Я",
    "а", "б", "в", "г", "д", "е", "ж", "з", "и", "й", "к", "л", "м", "н", "о", "п",
    "░", "▒", "▓", "│", "┤", "╡", "╢", "╖", "╕", "╣", "║", "╗", "╝", "╜", "╛", "┐",
    "└", "┴", "┬", "├", "─", "┼", "╞", "╟", "╚", "╔", "╩", "╦", "╠", "═", "╬", "╧",
    "╨", "╤", "╥", "╙", "╘", "╒", "╓", "╫", "╪", "┘", "┌", "█", "▄", "▌", "▐", "▀",
    "р", "с", "т", "у", "ф", "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я",
    "Ё", "ё", "Є", "є", "Ї", "ї", "Ў", "ў", "°", "∙", "·", "√", "№", "¤", "■", " "};

constexpr int kColumns = 80;

std::string asciiChar(uint8_t b)
{
    return (b >= 0x20 && b < 0x7F) ? std::string(1, static_cast<char>(b)) : ".";
}

/* The bytes of one UTF-8 character starting at `i`. */
size_t utf8Length(const std::string &s, size_t i)
{
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}

std::vector<std::string> textLines(std::span<const uint8_t> whole, const ViewOptions &opts)
{
    std::vector<std::string> out;
    std::string line;
    int column = 0;
    bool rus = false;
    auto flush = [&]() { out.push_back(line); line.clear(); column = 0; };
    for (const uint8_t b : textBody(whole)) {
        if (b == '\n') { flush(); continue; }
        if (b == '\r') continue;
        if (b == 0x0E || b == 0x0F) {
            if (opts.encoding == Encoding::koi7shift) { rus = (b == 0x0E); continue; }
        }
        if (b == '\t') {
            do { line += ' '; ++column; } while (column % 8 != 0);
        } else {
            line += decodeByte(b, opts.encoding, rus);
            ++column;
        }
        if (opts.wrap && column >= kColumns) flush();
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

/* The characters of one line of a dump, in the viewer's encoding: a
 * control byte is a dot, the rest is what the encoding makes of it. */
std::string gutterOf(std::span<const uint8_t> bytes, size_t from, size_t to, Encoding encoding, bool &rus)
{
    std::string out;
    for (size_t i = from; i < to && i < bytes.size(); ++i) {
        const uint8_t b = bytes[i];
        if (b < 0x20 || b == 0x7F) { (void)decodeByte(b, encoding, rus); out += "."; continue; }
        out += decodeByte(b, encoding, rus);
    }
    return out;
}

std::vector<std::string> octalLines(std::span<const uint8_t> bytes, const ViewOptions &opts)
{
    std::vector<std::string> out;
    bool rus = false;
    for (size_t off = 0; off < bytes.size(); off += 16) {
        std::string line = fmt::format("{:06o}:", off);
        for (size_t i = off; i < off + 16; i += 2) {
            if (i >= bytes.size()) { line += "       "; continue; }
            const unsigned lo = bytes[i];
            const unsigned hi = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            line += fmt::format(" {:06o}", (hi << 8) | lo);
        }
        /* the characters at the right, as the machine's own DUMP prints
         * them - a space, then the sixteen, and the line is 80 columns */
        line += " " + gutterOf(bytes, off, off + 16, opts.encoding, rus);
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> hexLines(std::span<const uint8_t> bytes, const ViewOptions &opts)
{
    std::vector<std::string> out;
    bool rus = false;
    for (size_t off = 0; off < bytes.size(); off += 16) {
        std::string line = fmt::format("{:06x}:", off);
        for (size_t i = off; i < off + 16; ++i) {
            if (i == off + 8) line += ' ';                  /* the halves apart */
            line += i < bytes.size() ? fmt::format(" {:02x}", bytes[i]) : "   ";
        }
        /* the characters at the right, as in the octal dump */
        line += " " + gutterOf(bytes, off, off + 16, opts.encoding, rus);
        out.push_back(line);
    }
    return out;
}

} // namespace

std::string decodeByte(uint8_t byte, Encoding encoding, bool &rusShift)
{
    switch (encoding) {
    case Encoding::ascii:
        return asciiChar(byte);
    case Encoding::koi7:
        if (byte >= 0x60 && byte <= 0x7E) return kKoi7[byte - 0x60];
        return asciiChar(byte);
    case Encoding::koi7shift:
        if (byte == 0x0E) { rusShift = true; return ""; }
        if (byte == 0x0F) { rusShift = false; return ""; }
        if (rusShift && byte >= 0x60 && byte <= 0x7E) return kKoi7[byte - 0x60];
        if (rusShift && byte >= 0x40 && byte <= 0x5E) return kKoi7[byte - 0x40];
        return asciiChar(byte);
    case Encoding::koi8:
    case Encoding::koi8rod:
        if (byte >= 0xE0) return kKoi8Upper[byte - 0xE0];
        if (byte >= 0xC0) return kKoi8Lower[byte - 0xC0];
        if (byte >= 0x80) return (encoding == Encoding::koi8 ? kRomBGraph : kRodGraph)[byte - 0x80];
        return asciiChar(byte);
    case Encoding::cp866:
        if (byte >= 0x80) return kCp866[byte - 0x80];
        return asciiChar(byte);
    }
    return ".";
}

std::span<const uint8_t> textBody(std::span<const uint8_t> bytes)
{
    size_t end = bytes.size();
    while (end > 0 && bytes[end - 1] == 0) --end;
    if (end > 0 && bytes[end - 1] == 0x1A) --end;
    return bytes.first(end);
}

bool isTextLike(std::span<const uint8_t> bytes)
{
    for (const uint8_t b : textBody(bytes)) {
        if (b >= 0x20 && b != 0x7F) continue;               /* printable ASCII and the 8-bit letters */
        switch (b) {
        case 0x09: case 0x0A: case 0x0D: case 0x0C:         /* tab, the line ends, form feed */
        case 0x0E: case 0x0F:                                /* the KOI-7 shifts */
        case 0x1B:                                           /* escape sequences of the terminal */
            continue;
        default:
            return false;
        }
    }
    return true;
}

Encoding detectEncoding(std::span<const uint8_t> whole)
{
    const auto bytes = textBody(whole);
    size_t high = 0, lowerRange = 0, koi7Marks = 0;
    for (const uint8_t b : bytes) {
        if (b == 0x0E || b == 0x0F) return Encoding::koi7shift;
        if (b >= 0x80) {
            ++high;
            continue;
        }
        if (b >= 0x60 && b <= 0x7E) {
            ++lowerRange;
            switch (b) {
            case 'q': case 'j': case 'x': case '`': case '{': case '|': case '}': case '~': ++koi7Marks; break;
            default: break;
            }
        }
    }
    if (high > 0) {
        /* whose letters make words and whose lines join: KOI-8's letters
         * at 0xC0..0xFF and its frames at 0x80..0xBF, or CP866's letters at
         * 0x80..0xAF and 0xE0..0xF1 and its frames at 0xB0..0xDF */
        auto graph = [](const std::array<const char *, 64> &t) {
            return [&t](uint8_t b) -> const char * { return b >= 0x80 && b < 0xC0 ? t[b - 0x80] : nullptr; };
        };
        const int romB = joins(bytes, graph(kRomBGraph));
        const int rod = joins(bytes, graph(kRodGraph));
        const int cpJoins = joins(bytes, [](uint8_t b) -> const char * { return b >= 0x80 ? kCp866[b - 0x80] : nullptr; });
        const long koi8 = static_cast<long>(letterPairs(bytes, [](uint8_t b) { return b >= 0xC0; })) + std::max({romB, rod, 0});
        const long cp866 = static_cast<long>(letterPairs(bytes, [](uint8_t b) {
                               return (b >= 0x80 && b <= 0xAF) || (b >= 0xE0 && b <= 0xF1); })) + std::max(cpJoins, 0);
        if (cp866 > koi8) return Encoding::cp866;
        /* KOI-8: ROM-B's pseudographics unless Rodionov's lines join better */
        return rod > romB ? Encoding::koi8rod : Encoding::koi8;
    }
    if (lowerRange == 0) return Encoding::ascii;
    return koi7Marks * 100 >= lowerRange * 3 ? Encoding::koi7 : Encoding::ascii;
}

std::vector<std::string> renderLines(std::span<const uint8_t> bytes, const ViewOptions &opts)
{
    switch (opts.view) {
    case View::text:  return textLines(bytes, opts);
    case View::octal: return octalLines(bytes, opts);
    case View::hex:   return hexLines(bytes, opts);
    }
    return {};
}

std::optional<std::vector<uint8_t>> encodeString(const std::string &utf8, Encoding encoding)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < utf8.size();) {
        const size_t n = utf8Length(utf8, i);
        const std::string ch = utf8.substr(i, n);
        i += n;
        bool found = false;
        for (int b = 0x20; b < 0x100 && !found; ++b) {
            if (encoding == Encoding::ascii && b >= 0x7F) break;
            bool rus = true;               /* koi7shift: search the РУС row */
            if (decodeByte(static_cast<uint8_t>(b), encoding, rus) == ch) {
                out.push_back(static_cast<uint8_t>(b));
                found = true;
            }
        }
        if (!found) return std::nullopt;
    }
    return out;
}

std::optional<size_t> findBytes(std::span<const uint8_t> bytes, std::span<const uint8_t> needle, size_t from)
{
    if (needle.empty() || needle.size() > bytes.size()) return std::nullopt;
    for (size_t i = from; i + needle.size() <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, needle.data(), needle.size()) == 0) return i;
    return std::nullopt;
}

const char *viewName(View view)
{
    switch (view) {
    case View::text:  return "text";
    case View::octal: return "octal";
    case View::hex:   return "hex";
    }
    return "";
}

const char *encodingName(Encoding encoding)
{
    switch (encoding) {
    case Encoding::ascii:     return "ASCII";
    case Encoding::koi8:      return "KOI-8";
    case Encoding::koi8rod:   return "KOI-8 Rodionov";
    case Encoding::koi7:      return "KOI-7";
    case Encoding::koi7shift: return "KOI-7 ^N/^O";
    case Encoding::cp866:     return "CP866";
    }
    return "";
}

View nextView(View view)
{
    return view == View::text ? View::octal : view == View::octal ? View::hex : View::text;
}

Encoding nextEncoding(Encoding encoding)
{
    switch (encoding) {
    case Encoding::ascii:     return Encoding::koi8;
    case Encoding::koi8:      return Encoding::koi8rod;
    case Encoding::koi8rod:   return Encoding::koi7;
    case Encoding::koi7:      return Encoding::koi7shift;
    case Encoding::koi7shift: return Encoding::cp866;
    case Encoding::cp866:     return Encoding::ascii;
    }
    return Encoding::ascii;
}

} /* namespace ms0515::files */
