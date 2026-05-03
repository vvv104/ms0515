/*
 * Terminal.hpp — VRAM-decoded scrollback mirror.
 *
 * Owns the entire VRAM → host-terminal pipeline:
 *
 *   1. Decode VRAM into an 80×25 (or 40×25 in lores) cell snapshot of
 *      KOI-8 codes, using a font map built from the current ROM.
 *   2. Diff the snapshot against the previous one and emit just the
 *      delta — characters appended at end-of-line, scroll-up content,
 *      or a full redraw — to an in-memory history string and (if
 *      configured) to a host FILE*.
 *
 * Two patterns the diff classifier recognises while the OS is at a
 * command prompt:
 *
 *   - Append    — characters added at the end of one row (echoed
 *                 user input or freshly printed output).
 *   - Scroll-up — top rows fall off, the bottom rows are new
 *                 (OS printed past the last screen line).
 *   - Redraw    — anything else (clear-screen, big rewrite, mode
 *                 change).  Emits the full current screen.
 *
 * What it intentionally does NOT do:
 *   - Position the host-terminal cursor with ANSI escapes.  We rely
 *     on the host's natural left-to-right / top-to-bottom flow plus
 *     `\n` so scrolled-off lines fall into the host's native scroll
 *     buffer.  That gives the user back the scrollback the MS-0515
 *     does not have on its own 25-line display.
 *   - Reflect cursor blink (the OS draws a '_' that toggles on/off).
 *     The cursor character is treated as a transparent blank for
 *     diffing — see `setCursorChar`.
 *
 * Lifecycle: a single `Terminal` instance lives for the life of the
 * Emulator.  Call `update(emu)` once per emulator frame; the font map
 * is rebuilt automatically when the ROM changes (detected via the
 * Emulator's ROM CRC).
 */

#ifndef MS0515_TERMINAL_HPP
#define MS0515_TERMINAL_HPP

#include <ms0515/Emulator.hpp>     /* ms0515::Emulator */

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ms0515 {

class Terminal {
public:
    /* ── Constants ────────────────────────────────────────────────────── */

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
     * decoded" marker. */
    static constexpr uint8_t kUnknownGlyph = 0x7F;

    /* Cell code for OS-drawn glyphs that aren't part of KOI-8R but
     * the OS still paints into VRAM (e.g. the © used by the Rodionov
     * 1992 banner).  Borrowed from Latin-1 0xA9 so the code stays in
     * a byte and reuses the existing emitter pipeline. */
    static constexpr uint8_t kCopyrightSign = 0xA9;

    /* ── Snapshot ─────────────────────────────────────────────────────── */

    /* 25 × kHiresCols cells of decoded KOI-8.  In lores mode only the
     * first kLoresCols of each row are meaningful; the rest hold
     * 0x20 (blank). */
    struct Snapshot {
        int cols = 0;                                       /* 40 or 80 */
        std::array<uint8_t, kHiresCols * kRows> cells{};

        [[nodiscard]] std::string row(int r) const;
    };

    /* ── KOI-8 → UTF-8 emitters ───────────────────────────────────────── */

    /* Emit one KOI-8 cell to a FILE*.  Printable ASCII passes through;
     * KOI-8R upper-half (0xC0–0xFF) is translated to UTF-8 Cyrillic;
     * kUnknownGlyph → █ (U+2588); kCopyrightSign → © (U+00A9);
     * everything else becomes '.'. */
    static void putKoi8Char(FILE *f, uint8_t koi8);

    /* Same translation, but appends to a std::string. */
    static void appendKoi8Char(std::string &dst, uint8_t koi8);

    /* ── Construction / configuration ─────────────────────────────────── */

    Terminal();

    /* Set the host-side stream.  Pass nullptr to disable mirroring to
     * the host; the in-memory history keeps accumulating either way. */
    void setOutput(FILE *f) noexcept { out_ = f; }

    /* Override the set of "transparent" characters — code points
     * collapsed to a space when computing the trimmed row used for
     * diff classification.  Default is `_` (the OS-drawn cursor). */
    void setTransparentChars(std::string_view chars)
        { transparentChars_ = chars; }

    /* Backwards-compatible single-char setter. */
    void setCursorChar(uint8_t c) {
        transparentChars_.assign(1, static_cast<char>(c));
    }

    /* Drop the diff shadow so the next update emits the full current
     * screen as the new initial state. */
    void reset() noexcept;

    /* ── Live sampling (frontend entry points) ────────────────────────── */

    /* Decode the current VRAM into a Snapshot.  The font map is
     * rebuilt automatically when the Emulator's ROM CRC changes —
     * frontends never need to call buildFont themselves.  Does NOT
     * advance the diff history. */
    [[nodiscard]] Snapshot decode(const Emulator &emu);

    /* Sample the current screen and run it through the stability
     * gates → diff classifier → history.  Equivalent to
     * `feedSample(decode(emu))`.  Call once per host frame from the
     * sampling loop; the gates drop snapshots caught mid-update. */
    void update(const Emulator &emu);

    /* ── Snapshot-based entry points (test helpers) ──────────────────── */

    /* Compare `snap` against the previous frame and emit just the
     * delta, bypassing the stability gates.  Tests pass hand-crafted
     * Snapshots straight to this entry point. */
    void update(const Snapshot &snap);

    /* Same path as `update(emu)` but with a hand-crafted snap (runs
     * the clean / progressing / no-adjacent-duplicate gates).  */
    void feedSample(const Snapshot &snap);

    /* ── Output access ────────────────────────────────────────────────── */

    /* Read-only access to every byte the mirror has ever emitted.
     * UI code can hand the underlying null-terminated buffer to
     * ImGui::InputTextMultiline. */
    [[nodiscard]] const std::string &history() const noexcept
        { return history_; }

    /* Discard the accumulated history.  Does not touch the diff
     * shadow — call `reset()` if you want both. */
    void clearHistory() noexcept
        { history_.clear(); lastScreenStart_ = 0; }

    /* Byte offset in history() where the most recent "full-screen
     * redraw" begins.  UI code uses this to anchor the scrollback
     * view to the start of the new screen so it visually mimics an
     * OS terminal redraw. */
    [[nodiscard]] std::size_t lastScreenStart() const noexcept
        { return lastScreenStart_; }

private:
    /* ── Decode pipeline (formerly ScreenReader) ──────────────────────── */

    /* Drop the per-cell glyph cache.  The ROM-CRC check forces this
     * automatically when the font map is rebuilt; the diff path also
     * calls it after a snapshot wholesale-replace via loadState. */
    void invalidateDecodeCache() noexcept { cacheValid_ = false; }

    /* Build the KOI-8 → 8×8-glyph map from `rom`.  Anchors are
     * KOI-8 '0' (main font) and Cyrillic 'А' (alt font); the table
     * bases are derived by locating those glyph bitmaps in the ROM,
     * so different ROM revisions with shifted font tables still
     * resolve correctly. */
    void buildFont(std::span<const uint8_t> rom);

    /* Look up a 64-bit glyph key in the font map.  Falls back to
     * 0x20 (blank) for sparse-pixel patterns and to kUnknownGlyph
     * for everything else — see lookup() comments for the rationale. */
    [[nodiscard]] uint8_t lookup(std::uint64_t key) const;

    /* Decode every cell of `vram` into the cached Snapshot, reusing
     * the per-cell cache for cells whose bitmap has not changed
     * since the previous call.  A hires↔lores switch drops the
     * cache automatically. */
    [[nodiscard]] const Snapshot &readScreen(std::span<const uint8_t> vram,
                                             bool hires);

    /* Pack an 8-byte glyph bitmap into a single 64-bit key. */
    [[nodiscard]] static std::uint64_t glyphKey(const uint8_t glyph[8]);

    /* Read one cell's 64-bit glyph key from VRAM.  The hires variant
     * uses bytesPerLine=80; the lores variant samples even bytes
     * (pixel data lives in the low byte of each VRAM word). */
    [[nodiscard]] static std::uint64_t readCell(std::span<const uint8_t> vram,
                                                int col, int row,
                                                int bytesPerLine);
    [[nodiscard]] static std::uint64_t readCellLores(std::span<const uint8_t> vram,
                                                     int col, int row);

    /* ── Diff classifier (existing Terminal logic) ────────────────────── */

    /* Length of `s.row(r)` after stripping trailing blanks AND
     * treating cursorChar_ as a blank.  Returns the byte string
     * directly because every caller wants both the length and the
     * content. */
    [[nodiscard]] std::string trimmedRow(const Snapshot &s, int r) const;

    /* Returns k > 0 if the new screen is the shadow scrolled up by
     * exactly k rows AND the preserved overlap actually contains
     * non-blank content.  Returns 0 if no useful scroll is found. */
    [[nodiscard]] int detectScrollUp(const Snapshot &cur) const;

    /* True if shadow and cur agree on every trimmed row. */
    [[nodiscard]] bool isUnchanged(const Snapshot &cur) const;

    /* Try to express the diff as an in-order sequence of row
     * updates.  Returns false → caller falls through to the dedup
     * path. */
    [[nodiscard]] bool tryEmitIncremental(const Snapshot &cur);

    /* Set-diff fallback: emit only the lines from `cur` that aren't
     * already present in the shadow. */
    void emitDedup(const Snapshot &cur);

    /* Emit the trimmed text of `row` from `snap`. */
    void emitRowLine(const Snapshot &snap, int row);
    void emitRowText(const Snapshot &snap, int row);

    /* Index of the highest non-empty trimmed row, or -1. */
    [[nodiscard]] int lastNonEmptyRow(const Snapshot &s) const;

    /* Output fan-out helpers (FILE* + history string). */
    void emitChar(char c);
    void emitText(std::string_view s);
    void emitKoi8(uint8_t koi8);

    /* ── Decode state ─────────────────────────────────────────────────── */

    std::unordered_map<std::uint64_t, uint8_t>           glyphMap_;
    std::array<std::uint64_t, kHiresCols * kRows>        cachedKeys_{};
    Snapshot                                              cachedSnap_;
    int                                                   cachedCols_  = 0;
    bool                                                  cacheValid_  = false;
    std::uint32_t                                         fontRomCrc_  = 0;
    bool                                                  fontBuilt_   = false;

    /* ── Diff state ───────────────────────────────────────────────────── */

    Snapshot    shadow_;
    bool        hasShadow_       = false;
    std::string transparentChars_ = "_";
    FILE       *out_             = nullptr;
    std::string history_;

    /* Logical row of the shadow that the host terminal's cursor is
     * currently parked on. */
    int         lastEmitRow_     = -1;

    /* Trimmed content of the line the host cursor is currently
     * parked on. */
    std::string lastEmittedLine_;

    /* feedSample() state — the last raw snapshot we forwarded to
     * update(). */
    Snapshot    lastForwardedSnap_;
    bool        hasLastForwardedSnap_ = false;

    /* Byte offset in history_ where the most recent "screen redraw"
     * starts. */
    std::size_t lastScreenStart_ = 0;
};

} /* namespace ms0515 */

#endif /* MS0515_TERMINAL_HPP */
