/*
 * ScreenReader.hpp — VRAM text extraction.
 *
 * Reads the VRAM bitmap, matches 8x8 pixel blocks against the ROM
 * font, and exposes the result either as a streamed dump (for the
 * frontend's headless debug log) or as an in-memory snapshot of
 * KOI-8 codes (for tests that need to assert on screen content).
 *
 * In hires mode (640x200) the screen is 80 columns × 25 rows.
 * In lores mode (320x200) the screen is 40 columns × 25 rows.
 *
 * The ROM font is at address 167336 (octal), starting from character
 * code 0x20 (space), with 8 bytes per glyph.
 */

#ifndef MS0515_SCREEN_READER_HPP
#define MS0515_SCREEN_READER_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>

namespace ms0515 {

class ScreenReader {
public:
    static constexpr int kHiresCols = 80;
    static constexpr int kLoresCols = 40;
    static constexpr int kRows      = 25;
    static constexpr int kGlyphH    = 8;

    /* Snapshot of the screen as KOI-8 codes — kRows × kHiresCols cells.
     * In lores mode only the first kLoresCols of each row are meaningful;
     * the rest are filled with 0x20 (blank). */
    struct Snapshot {
        int cols = 0;                                       /* 40 or 80 */
        std::array<uint8_t, kHiresCols * kRows> cells{};

        /* Extract a single row as a KOI-8 byte string with trailing
         * blanks trimmed.  Bytes 0x20–0x7E are plain ASCII; 0xC0–0xFF
         * are Cyrillic glyphs in the alt font. */
        [[nodiscard]] std::string row(int r) const;
    };

    ScreenReader();

    void setOutput(FILE *f) noexcept { out_ = f; }

    void buildFont(std::span<const uint8_t> rom);

    /* Streaming update: matches each cell, dumps to `out_` when the
     * frame differs from the previous one.  No-op if `out_` is null. */
    void update(std::span<const uint8_t> vram, bool hires);

    /* Force a full dump of the current screen, even if unchanged. */
    void dumpFull(std::span<const uint8_t> vram, bool hires);

    /* Read the current screen into a Snapshot without printing.  Pure
     * function — does not touch `prev_`/`hasPrev_` or `out_`. */
    [[nodiscard]] Snapshot readScreen(std::span<const uint8_t> vram,
                                      bool hires) const;

private:
    static uint64_t glyphKey(const uint8_t glyph[8]);
    static uint64_t readCell(std::span<const uint8_t> vram, int col, int row,
                             int bytesPerLine);
    static uint64_t readCellLores(std::span<const uint8_t> vram, int col, int row);
    [[nodiscard]] uint8_t lookup(uint64_t key) const;
    static void putKoi8Char(FILE *f, uint8_t koi8);
    void dumpScreen(const uint8_t *screen, int cols);

    std::unordered_map<uint64_t, uint8_t> glyphMap_;
    std::array<uint8_t, kHiresCols * kRows> prev_{};
    bool hasPrev_ = false;
    FILE *out_ = nullptr;
};

} /* namespace ms0515 */

#endif /* MS0515_SCREEN_READER_HPP */
