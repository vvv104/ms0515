/*
 * ScreenReader.cpp — VRAM text extraction.
 */

#include <ms0515/ScreenReader.hpp>
#include <ms0515/Emulator.hpp>
#include <algorithm>
#include <bit>
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
    cachedSnap_.cells.fill(0x20);
    lastDumped_.fill(0x20);
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
    /* Sparse-pixel cells: any unmatched bitmap with fewer than 17
     * set pixels (out of 64) is a thin horizontal-bar / leftover
     * pattern.  At Hi-Res 640×200 → 1×2 framebuffer scaling those
     * are barely-visible 1–2 pixel lines that the user reads as
     * blank space; emitting █ for them in scrollback creates noise
     * the user explicitly objected to.  Diag run on Rodionov OSA-B
     * showed the persistent "stray █ at column 0" cells were exactly
     * these (popcount 8 and 16, e.g. `00 00 00 00 00 00 ff 00` and
     * `00 00 00 00 00 00 ff ff`).  Anything denser stays as
     * kUnknownGlyph and renders as █ — those are real visible
     * unknown glyphs (custom bitmaps the OS draws, e.g. ©). */
    if (std::popcount(key) < 17)
        return 0x20;
    return kUnknownGlyph;
}

/* UTF-8 encoding of the "full block" character █ (U+2588) — the
 * visible marker we emit for cells that decoded to kUnknownGlyph.
 * Three bytes: 11100010 10010110 10001000. */
static constexpr char kUnknownGlyphUtf8[] = "\xE2\x96\x88";

/* UTF-8 encoding of the copyright sign © (U+00A9) — the
 * representation we emit for kCopyrightSign cells (the Rodionov
 * banner glyph and any other OS-drawn ©). */
static constexpr char kCopyrightUtf8[] = "\xC2\xA9";

void ScreenReader::putKoi8Char(FILE *f, uint8_t koi8)
{
    if (koi8 == kUnknownGlyph) {
        std::fputs(kUnknownGlyphUtf8, f);
    } else if (koi8 == kCopyrightSign) {
        std::fputs(kCopyrightUtf8, f);
    } else if (koi8 >= 0x20 && koi8 < 0x7F) {
        std::fputc(koi8, f);
    } else if (koi8 >= 0xC0) {
        const char *utf = koi8r_to_utf8(koi8);
        if (utf)
            std::fputs(utf, f);
        else
            std::fputs(kUnknownGlyphUtf8, f);
    } else {
        std::fputc('.', f);
    }
}

void ScreenReader::appendKoi8Char(std::string &dst, uint8_t koi8)
{
    if (koi8 == kUnknownGlyph) {
        dst.append(kUnknownGlyphUtf8);
    } else if (koi8 == kCopyrightSign) {
        dst.append(kCopyrightUtf8);
    } else if (koi8 >= 0x20 && koi8 < 0x7F) {
        dst.push_back(static_cast<char>(koi8));
    } else if (koi8 >= 0xC0) {
        const char *utf = koi8r_to_utf8(koi8);
        if (utf)
            dst.append(utf);
        else
            dst.append(kUnknownGlyphUtf8);
    } else {
        dst.push_back('.');
    }
}

/*
 * Find the file offset of an 8-byte glyph bitmap in the ROM by visual
 * shape (i.e. an exact bitmap-pattern match — same visible pixels is
 * the only thing that matters; we don't care which character code the
 * ROM stores at that position).  Returns -1 if the anchor is not
 * present in the ROM.
 *
 * The anchors below are the canonical ROM-A glyphs for two characters
 * whose shape is visually distinctive enough that a chance collision
 * with unrelated data is vanishingly unlikely:
 *
 *   - main font anchor: KOI-8 '0' (0x30) — the digit zero, an oval
 *     that no surrounding code/data table happens to encode;
 *   - alt font anchor:  KOI-8 'А' uppercase (0xE1, alt index 33) —
 *     the Cyrillic capital A, a wide pyramid shape.
 *
 * Since each anchor lives at a fixed offset *within* its font table
 * (kAnchorMainCode-0x20 glyphs into the main font, kAnchorAltIndex
 * glyphs into the alt font), once we locate the anchor's bytes in
 * the ROM we subtract that offset to find the table base.  This
 * frees the screen reader from assuming any specific ROM revision —
 * different MS0515 ROM-A/ROM-B builds shift the font tables but
 * keep the glyph shapes identical.
 */
static int findFontBase(std::span<const uint8_t> rom,
                        const uint8_t (&anchor)[8],
                        int anchorIndex)
{
    if (rom.size() < 8) return -1;
    const auto end = rom.size() - 8;
    for (size_t off = 0; off <= end; ++off) {
        if (std::memcmp(rom.data() + off, anchor, 8) == 0) {
            const int base = static_cast<int>(off) - anchorIndex * 8;
            if (base >= 0)
                return base;
        }
    }
    return -1;
}

void ScreenReader::buildFont(std::span<const uint8_t> rom)
{
    glyphMap_.clear();
    /* Decoded chars in the cell cache reflect the OLD font; drop them
     * so the next readScreen re-decodes everything against the new map. */
    invalidateCache();

    /*
     * Main-font anchor: the visual shape of '0' (KOI-8 0x30).  Glyphs
     * are 8 bytes tall, one byte per row, MSB = leftmost pixel.
     */
    static constexpr uint8_t kAnchorZero[8] = {
        0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
    };
    /*
     * Alt-font anchor: the visual shape of 'А' (KOI-8 0xE1).  Lives
     * at alt index 33 (= (0xE1 & 0x7F) - 0x40).
     */
    static constexpr uint8_t kAnchorCyrA[8] = {
        0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00,
    };
    static constexpr int kAnchorMainCode  = 0x30;
    static constexpr int kAnchorAltIndex  = 33;

    const int kMainFontFileOff = findFontBase(rom, kAnchorZero,
                                              kAnchorMainCode - 0x20);
    const int kAltFontFileOff  = findFontBase(rom, kAnchorCyrA,
                                              kAnchorAltIndex);

    const auto romSize = rom.size();

    /*
     * Register glyphs from the main font (codes 0x20–0x7F).  Use
     * insert-if-absent so EARLIER codes win on a glyph collision —
     * this is critical because the ROM stores identical pixel
     * patterns for several pairs (e.g. '@' 0x40 and '`' 0x60), and
     * we want the printable ASCII letter / symbol to be the canonical
     * lookup result rather than the backtick that happens to share
     * the bitmap.
     */
    if (kMainFontFileOff >= 0) {
        for (int code = 0x20; code < 0x80; ++code) {
            int off = kMainFontFileOff + (code - 0x20) * 8;
            if (off + 8 > static_cast<int>(romSize))
                break;
            uint64_t key = glyphKey(rom.data() + off);
            if (key != 0 || code == 0x20)
                glyphMap_.emplace(key, static_cast<uint8_t>(code));
        }
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
    /*
     * Add Cyrillic codes only for patterns the main font did not
     * already claim — keeps printable ASCII as the canonical answer
     * for ambiguous glyphs (e.g. '_' 0x5F vs Ъ 0xFF, identical 8x8).
     */
    if (kAltFontFileOff >= 0) {
        for (int code = 0xC0; code <= 0xFF; ++code) {
            int glyph_idx = (code & 0x7F) - 0x40;
            int off = kAltFontFileOff + glyph_idx * 8;
            if (off < 0 || off + 8 > static_cast<int>(romSize))
                continue;
            uint64_t key = glyphKey(rom.data() + off);
            if (key != 0)
                glyphMap_.emplace(key, static_cast<uint8_t>(code));
        }
    }

    /*
     * OS-drawn glyphs that aren't fetched from the ROM font tables.
     * Some applications draw their own bitmaps via direct VRAM
     * writes (e.g. boot banners with stylised symbols), and those
     * patterns never appear in either ROM font — so without an
     * explicit map entry they leak into scrollback as █ unknown
     * markers.  Each entry below was extracted from a real boot
     * trace; insertion is `try_emplace` so a ROM-supplied glyph
     * with the same shape would still take precedence. */
    static constexpr struct { uint64_t key; uint8_t code; } kCustomGlyphs[] = {
        /* © as drawn by the Rodionov 1992 OSA boot banner: an 8×8
         * circle with an internal "C" — bytes
         * 3c 42 99 a1 a1 99 42 3c (popcount 26).  KOI-8R has no
         * copyright sign, so we map it to kCopyrightSign (Latin-1
         * 0xA9) and let appendKoi8Char/putKoi8Char emit the UTF-8
         * sequence. */
        { 0x3C4299A1A199423CULL, kCopyrightSign },
    };
    for (const auto &g : kCustomGlyphs)
        glyphMap_.try_emplace(g.key, g.code);
}

void ScreenReader::update(std::span<const uint8_t> vram, bool hires)
{
    if (!out_) return;

    /* Use the cached read so we don't redo the per-cell glyph lookup
     * on every frame.  Then check whether anything has changed since
     * the last time we actually emitted a dump. */
    const Snapshot &snap = readScreen(vram, hires);

    bool changed = !hasLastDumped_ ||
                   std::memcmp(snap.cells.data(), lastDumped_.data(),
                               snap.cells.size()) != 0;
    if (changed) {
        dumpScreen(snap.cells.data(), snap.cols);
        lastDumped_     = snap.cells;
        hasLastDumped_  = true;
    }
}

void ScreenReader::dumpFull(std::span<const uint8_t> vram, bool hires)
{
    if (!out_) return;
    hasLastDumped_ = false;
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

/* ── Cached read ────────────────────────────────────────────────────────── */
/*
 * For each cell we re-extract the 8-byte glyph bitmap from VRAM
 * (cheap — eight byte loads + bit shifts) and compare its 64-bit key
 * against `cachedKeys_`.  Cells whose key matches keep the decoded
 * code from `cachedSnap_`; only mismatched cells go through the
 * unordered_map font lookup.  On a static screen the inner branch
 * collapses to a key compare per cell, which is what the upcoming
 * terminal mode wants when polling every frame.
 *
 * A mode change (hires↔lores) invalidates the cache because the
 * column count and the cell-to-VRAM mapping both differ.
 */
ScreenReader::Snapshot ScreenReader::readScreen(std::span<const uint8_t> vram,
                                                bool hires)
{
    const int cols = hires ? kHiresCols : kLoresCols;
    const bool useCache = cacheValid_ && cachedCols_ == cols;

    if (!useCache) {
        cachedSnap_.cols = cols;
        /* Pre-fill so cells outside the active column range
         * (lores: 40..79) read back as blanks. */
        cachedSnap_.cells.fill(0x20);
    }

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int idx = row * kHiresCols + col;
            const uint64_t key = hires
                ? readCell(vram, col, row, 80)
                : readCellLores(vram, col, row);
            if (useCache && cachedKeys_[idx] == key)
                continue;
            cachedKeys_[idx]        = key;
            cachedSnap_.cells[idx]  = lookup(key);
        }
    }

    cachedCols_  = cols;
    cacheValid_  = true;
    return cachedSnap_;
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
