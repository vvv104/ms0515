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

    /* Sentinel byte stored in a Snapshot cell when the underlying
     * 8×8 bitmap doesn't match anything in the font map (the OS may
     * be drawing with a custom font we haven't resolved, or with
     * graphics glyphs the ROM doesn't carry).  We pick 0x7F (DEL)
     * because it can never appear as legitimate visible text — KOI-8
     * 0x7F is the control DEL.  putKoi8Char / appendKoi8Char render
     * it as █ (U+2588) so the user sees a clear "this cell wasn't
     * decoded" marker instead of a space (invisible) or '?' (looks
     * like real punctuation). */
    static constexpr uint8_t kUnknownGlyph = 0x7F;

    /* Cell code for OS-drawn glyphs that aren't part of KOI-8R but
     * the OS still paints into VRAM (e.g. the © used by the Rodionov
     * 1992 banner — KOI-8R has no copyright sign).  We borrow the
     * Latin-1 code point so the code stays in a byte and reuses the
     * existing emitter pipeline; appendKoi8Char / putKoi8Char
     * translate it to the UTF-8 sequence for ©. */
    static constexpr uint8_t kCopyrightSign = 0xA9;

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

    /* Read the current screen into a Snapshot.  Maintains an internal
     * per-cell cache of source bitmaps so cells whose 8-byte glyph in
     * VRAM has not changed since the previous call skip the
     * glyph-table lookup entirely.  The returned Snapshot is fully
     * populated either way; this is purely a speed optimisation for
     * the common case where most of the screen is static between
     * frames (and groundwork for the upcoming terminal mode, which
     * will poll readScreen on every frame). */
    Snapshot readScreen(std::span<const uint8_t> vram, bool hires);

    /* Drop the per-cell cache.  buildFont() and a hires↔lores switch
     * already invalidate it automatically; this is for callers that
     * have changed something the cache can't see (e.g. swapped the
     * VRAM buffer underlying a span). */
    void invalidateCache() noexcept { cacheValid_ = false; }

    /* Emit one KOI-8 code to `f`.  Printable ASCII (0x20..0x7E) goes
     * through verbatim; KOI-8R upper-half (0xC0..0xFF) is translated
     * to UTF-8 Cyrillic; everything else becomes '.'.  Exposed so
     * Terminal and other consumers don't reimplement the table. */
    static void putKoi8Char(FILE *f, uint8_t koi8);

    /* Same translation as putKoi8Char, but appends to a std::string
     * instead of writing to a FILE.  Useful for in-memory scrollback
     * buffers. */
    static void appendKoi8Char(std::string &dst, uint8_t koi8);

private:
    static uint64_t glyphKey(const uint8_t glyph[8]);
    static uint64_t readCell(std::span<const uint8_t> vram, int col, int row,
                             int bytesPerLine);
    static uint64_t readCellLores(std::span<const uint8_t> vram, int col, int row);
    [[nodiscard]] uint8_t lookup(uint64_t key) const;
    void dumpScreen(const uint8_t *screen, int cols);

    std::unordered_map<uint64_t, uint8_t> glyphMap_;

    /* Per-cell caches populated by readScreen().  cachedKeys_ holds
     * the 8-byte glyph bitmap (packed into uint64_t) seen at each
     * cell on the previous call; cachedSnap_ holds the decoded codes
     * from that same call.  cachedCols_ is the mode the cache was
     * built for so a hires↔lores switch forces a full re-decode. */
    std::array<uint64_t, kHiresCols * kRows> cachedKeys_{};
    Snapshot cachedSnap_;
    int  cachedCols_  = 0;
    bool cacheValid_  = false;

    /* Last screen the streaming update() actually emitted to `out_`.
     * Separate from the per-cell cache so the snapshot reader does
     * not have to know about output buffering. */
    std::array<uint8_t, kHiresCols * kRows> lastDumped_{};
    bool hasLastDumped_ = false;
    FILE *out_ = nullptr;
};

} /* namespace ms0515 */

#endif /* MS0515_SCREEN_READER_HPP */
