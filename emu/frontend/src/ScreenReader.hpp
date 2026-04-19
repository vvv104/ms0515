/*
 * ScreenReader.hpp — VRAM text extraction for console output.
 *
 * Reads the VRAM bitmap, matches 8x8 pixel blocks against the ROM font,
 * and outputs recognized text to stderr.  Used for headless debugging
 * and interactive terminal-style interaction with the emulated OS.
 *
 * In hires mode (640x200), the screen is 80 columns x 25 rows.
 * In lores mode (320x200), the screen is 40 columns x 25 rows.
 *
 * The ROM font is at address 167336 (octal), starting from character
 * code 0x20 (space), with 8 bytes per glyph.
 */

#ifndef MS0515_FRONTEND_SCREEN_READER_HPP
#define MS0515_FRONTEND_SCREEN_READER_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <unordered_map>

namespace ms0515_frontend {

class ScreenReader {
public:
    static constexpr int kHiresCols = 80;
    static constexpr int kLoresCols = 40;
    static constexpr int kRows      = 25;
    static constexpr int kGlyphH    = 8;

    ScreenReader();

    void setOutput(FILE *f) noexcept { out_ = f; }

    void buildFont(std::span<const uint8_t> rom);

    void update(std::span<const uint8_t> vram, bool hires);

    void dumpFull(std::span<const uint8_t> vram, bool hires);

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

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_SCREEN_READER_HPP */
