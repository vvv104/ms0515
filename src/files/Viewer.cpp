/*
 * Viewer.cpp — bytes to lines in the machine's encodings, octal, hex.
 */
#include "Viewer.hpp"

#include <fmt/format.h>

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
/* KOI-8R 0x80..0xBF: box drawing and the few symbols. */
constexpr std::array<const char *, 64> kKoi8Graph = {
    "─", "│", "┌", "┐", "└", "┘", "├", "┤", "┬", "┴", "┼", "▀", "▄", "█", "▌", "▐",
    "░", "▒", "▓", "⌠", "■", "∙", "√", "≈", "≤", "≥", " ", "⌡", "°", "²", "·", "÷",
    "═", "║", "╒", "ё", "╓", "╔", "╕", "╖", "╗", "╘", "╙", "╚", "╛", "╜", "╝", "╞",
    "╟", "╠", "╡", "Ё", "╢", "╣", "╤", "╥", "╦", "╧", "╨", "╩", "╪", "╫", "╬", "©"};

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

std::vector<std::string> octalLines(std::span<const uint8_t> bytes)
{
    std::vector<std::string> out;
    for (size_t off = 0; off < bytes.size(); off += 16) {
        std::string line = fmt::format("{:06o}:", off);
        for (size_t i = off; i < off + 16 && i < bytes.size(); i += 2) {
            const unsigned lo = bytes[i];
            const unsigned hi = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            line += fmt::format(" {:06o}", (hi << 8) | lo);
        }
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> hexLines(std::span<const uint8_t> bytes)
{
    std::vector<std::string> out;
    for (size_t off = 0; off < bytes.size(); off += 16) {
        std::string line = fmt::format("{:06x} ", off);
        std::string gutter;
        for (size_t i = off; i < off + 16; ++i) {
            if (i % 8 == 0) line += ' ';
            if (i < bytes.size()) {
                line += fmt::format("{:02x} ", bytes[i]);
                gutter += asciiChar(bytes[i]);
            } else {
                line += "   ";
            }
        }
        line += " |" + gutter + "|";
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
    case Encoding::koi8r:
        if (byte >= 0xE0) return kKoi8Upper[byte - 0xE0];
        if (byte >= 0xC0) return kKoi8Lower[byte - 0xC0];
        if (byte >= 0x80) return kKoi8Graph[byte - 0x80];
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
    size_t koi8 = 0, cp866 = 0, high = 0, lowerRange = 0, koi7Marks = 0;
    for (const uint8_t b : bytes) {
        if (b == 0x0E || b == 0x0F) return Encoding::koi7shift;
        if (b >= 0x80) {
            ++high;
            if (b >= 0xC0) ++koi8;
            if (b <= 0xAF || (b >= 0xE0 && b <= 0xF1)) ++cp866;
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
    if (high > 0) return cp866 > koi8 ? Encoding::cp866 : Encoding::koi8r;
    if (lowerRange == 0) return Encoding::ascii;
    return koi7Marks * 100 >= lowerRange * 3 ? Encoding::koi7 : Encoding::ascii;
}

std::vector<std::string> renderLines(std::span<const uint8_t> bytes, const ViewOptions &opts)
{
    switch (opts.view) {
    case View::text:  return textLines(bytes, opts);
    case View::octal: return octalLines(bytes);
    case View::hex:   return hexLines(bytes);
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
    case Encoding::koi8r:     return "KOI-8R";
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
    case Encoding::ascii:     return Encoding::koi8r;
    case Encoding::koi8r:     return Encoding::koi7;
    case Encoding::koi7:      return Encoding::koi7shift;
    case Encoding::koi7shift: return Encoding::cp866;
    case Encoding::cp866:     return Encoding::ascii;
    }
    return Encoding::ascii;
}

} /* namespace ms0515::files */
