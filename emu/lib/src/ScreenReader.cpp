/*
 * ScreenReader.cpp — VRAM text extraction.
 */

#include <ms0515/ScreenReader.hpp>
#include <ms0515/Emulator.hpp>
#include <algorithm>
#include <cstring>

namespace ms0515 {

/* ── KOI-8R to Unicode mapping for codes 0x80–0xFF ──────────────────────── */

static const char *koi8r_to_utf8(uint8_t code)
{
    /* KOI-8R upper half (0xC0–0xFF = Cyrillic А–я, plus specials) */
    static const char *koi8r_table[64] = {
        /* 0xC0 */ "ю", "а", "б", "ц", "д", "е", "ф", "г",
        /* 0xC8 */ "х", "и", "й", "к", "л", "м", "н", "о",
        /* 0xD0 */ "п", "я", "р", "с", "т", "у", "ж", "в",
        /* 0xD8 */ "ь", "ы", "з", "ш", "э", "щ", "ч", "ъ",
        /* 0xE0 */ "Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г",
        /* 0xE8 */ "Х", "И", "Й", "К", "Л", "М", "Н", "О",
        /* 0xF0 */ "П", "Я", "Р", "С", "Т", "У", "Ж", "В",
        /* 0xF8 */ "Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "Ъ",
    };

    if (code >= 0xC0)
        return koi8r_table[code - 0xC0];
    return nullptr;
}

/* ── ScreenReader implementation ────────────────────────────────────────── */

ScreenReader::ScreenReader()
{
    prev_.fill(0x20);
}

uint64_t ScreenReader::glyphKey(const uint8_t glyph[8])
{
    uint64_t key = 0;
    for (int i = 0; i < 8; ++i)
        key |= static_cast<uint64_t>(glyph[i]) << (i * 8);
    return key;
}

uint64_t ScreenReader::readCell(std::span<const uint8_t> vram, int col, int row,
                                int bytesPerLine)
{
    uint8_t glyph[8];
    for (int y = 0; y < kGlyphH; ++y) {
        int offset = (row * kGlyphH + y) * bytesPerLine + col;
        glyph[y] = vram[offset];
    }
    return glyphKey(glyph);
}

uint64_t ScreenReader::readCellLores(std::span<const uint8_t> vram, int col, int row)
{
    /* In lores (320x200), each scan line is 40 words = 80 bytes.
     * Pixel data is in the low byte of each word (even offsets).
     * Column c corresponds to word c → byte offset c*2. */
    uint8_t glyph[8];
    for (int y = 0; y < kGlyphH; ++y) {
        int offset = (row * kGlyphH + y) * 80 + col * 2;
        glyph[y] = vram[offset];
    }
    return glyphKey(glyph);
}

uint8_t ScreenReader::lookup(uint64_t key) const
{
    auto it = glyphMap_.find(key);
    if (it != glyphMap_.end())
        return it->second;
    if (key == 0)
        return 0x20;  /* All-zero = blank */
    return '?';
}

void ScreenReader::putKoi8Char(FILE *f, uint8_t koi8)
{
    if (koi8 >= 0x20 && koi8 < 0x7F) {
        std::fputc(koi8, f);
    } else if (koi8 >= 0xC0) {
        const char *utf = koi8r_to_utf8(koi8);
        if (utf)
            std::fputs(utf, f);
        else
            std::fputc('?', f);
    } else {
        std::fputc('.', f);
    }
}

void ScreenReader::buildFont(std::span<const uint8_t> rom)
{
    glyphMap_.clear();

    /* Main font at ROM address 167336 octal.
     * ROM file covers 140000–177777, so offset = 167336 - 140000 = 27336 octal. */
    static constexpr int kMainFontFileOff = 0'27336;  /* octal literal */
    /* Alt font at 170736 octal → offset 30736 octal. */
    static constexpr int kAltFontFileOff  = 0'30736;

    const auto romSize = rom.size();

    /* Register glyphs from the main font: codes 0x20–0x7F (ASCII) */
    for (int code = 0x20; code < 0x80; ++code) {
        int off = kMainFontFileOff + (code - 0x20) * 8;
        if (off + 8 > static_cast<int>(romSize))
            break;
        uint64_t key = glyphKey(rom.data() + off);
        if (key != 0 || code == 0x20)
            glyphMap_[key] = static_cast<uint8_t>(code);
    }

    /*
     * Cyrillic glyphs from the alt font (170736).
     *
     * ROM rendering logic (164716–164764):
     *   code &= 0x7F;                      // strip to 7-bit
     *   R0 = (code - 0x20) * 8;
     *   if (cyrillic_flag && R0 >= 256)     // i.e. code >= 0x60
     *       glyph = alt_font[R0 - 256]     // alt_font index = code - 0x60
     *
     * KOI-8 uppercase (0xE0–0xFF) → stripped to 0x60–0x7F → alt index 0–31
     * KOI-8 lowercase (0xC0–0xDF) → stripped to 0x40–0x5F → alt index via
     *   a separate code path (164700: ASL x2 + ADD 170736), where
     *   R0 = (code & 0x7F) * 4 → alt_font[(code & 0x7F) * 4].
     *
     * However, the simpler model works for uppercase: the alt font stores
     * 64 Cyrillic glyphs at indices 0–63, mapping KOI-8 codes 0xC0–0xFF
     * to alt font index (code & 0x7F) - 0x40.
     */
    for (int code = 0xC0; code <= 0xFF; ++code) {
        int glyph_idx = (code & 0x7F) - 0x40;
        int off = kAltFontFileOff + glyph_idx * 8;
        if (off < 0 || off + 8 > static_cast<int>(romSize))
            continue;
        uint64_t key = glyphKey(rom.data() + off);
        if (key != 0)
            glyphMap_[key] = static_cast<uint8_t>(code);
    }
}

void ScreenReader::update(std::span<const uint8_t> vram, bool hires)
{
    if (!out_) return;

    int cols = hires ? kHiresCols : kLoresCols;

    /* Scan all cells */
    bool changed = false;
    std::array<uint8_t, kHiresCols * kRows> screen;
    screen.fill(0x20);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < cols; ++col) {
            uint64_t key = hires
                ? readCell(vram, col, row, 80)
                : readCellLores(vram, col, row);
            uint8_t ch = lookup(key);
            screen[row * kHiresCols + col] = ch;
            if (!hasPrev_ || prev_[row * kHiresCols + col] != ch)
                changed = true;
        }
    }

    if (changed) {
        dumpScreen(screen.data(), cols);
        prev_ = screen;
        hasPrev_ = true;
    }
}

void ScreenReader::dumpFull(std::span<const uint8_t> vram, bool hires)
{
    if (!out_) return;
    hasPrev_ = false;
    update(vram, hires);
}

void ScreenReader::dumpScreen(const uint8_t *screen, int cols)
{
    if (!out_) return;

    std::fprintf(out_, "\n--- screen ---\n");

    for (int row = 0; row < kRows; ++row) {
        /* Find last non-space character to trim trailing blanks */
        int last = -1;
        for (int col = cols - 1; col >= 0; --col) {
            if (screen[row * kHiresCols + col] != 0x20) {
                last = col;
                break;
            }
        }
        if (last < 0) {
            std::fputc('\n', out_);
            continue;
        }
        for (int col = 0; col <= last; ++col) {
            putKoi8Char(out_, screen[row * kHiresCols + col]);
        }
        std::fputc('\n', out_);
    }

    std::fprintf(out_, "--- end ---\n");
    std::fflush(out_);
}

/* ── Pure read (no streaming, no side effects) ──────────────────────────── */

ScreenReader::Snapshot ScreenReader::readScreen(std::span<const uint8_t> vram,
                                                bool hires) const
{
    Snapshot snap;
    snap.cols = hires ? kHiresCols : kLoresCols;
    snap.cells.fill(0x20);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < snap.cols; ++col) {
            uint64_t key = hires
                ? readCell(vram, col, row, 80)
                : readCellLores(vram, col, row);
            snap.cells[row * kHiresCols + col] = lookup(key);
        }
    }
    return snap;
}

std::string ScreenReader::Snapshot::row(int r) const
{
    if (r < 0 || r >= kRows) return {};
    std::string s;
    s.reserve(static_cast<size_t>(cols));
    for (int c = 0; c < cols; ++c)
        s.push_back(static_cast<char>(cells[r * kHiresCols + c]));
    while (!s.empty() && static_cast<uint8_t>(s.back()) == 0x20)
        s.pop_back();
    return s;
}

} /* namespace ms0515 */
