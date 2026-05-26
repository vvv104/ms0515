/*
 * VramMirror.hpp — Write-hook-driven mirror of the 80×25 hires text plane.
 *
 * Subscribes to the C-core VRAM-write hook (Emulator::setVramWriteCallback)
 * and turns the stream of byte writes into a stream of ANSI-positioned
 * KOI-8 characters on a host FILE*.  Unlike Terminal, it never polls
 * VRAM — every byte the CPU stores is observed in order, so transient
 * paints don't slip between sampling frames.
 *
 * Lifecycle: attach(emu) once, call flushFrame() at the end of each
 * emulator frame (typically right after stepFrame).  flushFrame
 * inspects only the cells the hook flagged dirty, decodes their
 * current 8-byte glyph through the font map built from the active
 * ROM, and emits the delta as ANSI cursor positions + characters.
 *
 * The font map is built off the public `Emulator::romCrc32()` value;
 * it auto-rebuilds when the ROM CRC changes (loadRom, loadState).
 *
 * Lores mode (40 columns, color attributes) is NOT supported in the
 * first revision — VramMirror falls back to a no-op while the
 * emulator is in lores.  Hires (80 cols, monochrome) is what the
 * RT-11 monitor and the four shipped OSes use; lores support lands
 * later if it turns out to be needed.
 */

#ifndef MS0515_VRAM_MIRROR_HPP
#define MS0515_VRAM_MIRROR_HPP

#include <ms0515/Emulator.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ms0515 {

class VramMirror {
public:
    static constexpr int kCols   = 80;
    static constexpr int kRows   = 25;
    static constexpr int kGlyphH = 8;

    /* Sentinel byte for an 8×8 bitmap that's not in the font map.
     * Same value Terminal uses; rendered as █ (U+2588) so the user
     * sees a clear "this glyph is OS-drawn / RAM-loaded font" marker. */
    static constexpr uint8_t kUnknownGlyph = 0x7F;

    /* Cell code for OS-drawn glyphs that aren't part of KOI-8R (e.g.
     * the © used by the Rodionov 1992 banner).  Re-uses Latin-1 0xA9
     * so the emitter stack can stay byte-based. */
    static constexpr uint8_t kCopyrightSign = 0xA9;

    /* Up/down arrows drawn by Rodionov ROSA Commander between the
     * file panels.  KOI-8R has no codepoint for these so we steal
     * unused control-char slots.  Two variants per direction:
     *   pure triangle ▲ / ▼ — standalone shapes in OS dialogs.
     *   thick stem-arrow ⬆ / ⬇ — the between-panel scroll hints
     *   (triangle + vertical stem). */
    static constexpr uint8_t kArrowDownTri    = 0x1A;  /* ▼ */
    static constexpr uint8_t kArrowUpTri      = 0x1B;  /* ▲ */
    static constexpr uint8_t kArrowDownThick  = 0x1C;  /* ⬇ */
    static constexpr uint8_t kArrowUpThick    = 0x1D;  /* ⬆ */

    VramMirror();
    ~VramMirror();

    VramMirror(const VramMirror &)            = delete;
    VramMirror &operator=(const VramMirror &) = delete;
    VramMirror(VramMirror &&)                 = delete;
    VramMirror &operator=(VramMirror &&)      = delete;

    /* Subscribe to `emu`'s VRAM write callback and remember the emu
     * pointer for flushFrame() / snapshot().  Replaces any previous
     * attachment.  Calls Emulator::setVramWriteCallback under the hood,
     * so any prior callback is overwritten. */
    void attach(Emulator &emu);

    /* Clear the callback in the attached emulator and drop the pointer.
     * Safe to call when nothing was attached. */
    void detach();

    /* Set the host stream for ANSI-positioned char output.  Pass nullptr
     * to disable the mirror to FILE while keeping history accumulation
     * (history() always grows from the per-cell emissions). */
    void setOutput(FILE *f) noexcept { out_ = f; }

    /* Plain-text history.  One char per emit, in order.  No ANSI escape
     * sequences — host-terminal positioning lives in the FILE* path only. */
    [[nodiscard]] const std::string &history() const noexcept { return history_; }

    /* Discard accumulated history; keeps the cell shadow + cursor state. */
    void clearHistory() noexcept { history_.clear(); }

    /* Decoded position of the last cell that was emitted (row 0..kRows-1,
     * col 0..kCols-1).  Both -1 before the first emission. */
    [[nodiscard]] int cursorRow() const noexcept { return cursorRow_; }
    [[nodiscard]] int cursorCol() const noexcept { return cursorCol_; }

    /* Cell of the most recent VRAM write — typically where the guest
     * OS just stamped its cursor glyph (the OS-side blink target).
     * Both -1 before the first write. */
    [[nodiscard]] int lastWriteRow() const noexcept { return lastWriteRow_; }
    [[nodiscard]] int lastWriteCol() const noexcept { return lastWriteCol_; }

    /* Number of consecutive flushFrame() calls with zero VRAM writes
     * observed in between.  Reaches a non-trivial value while the
     * kernel is parked at a prompt and goes back to 0 the moment new
     * output starts streaming.  Used by the CLI as a "kernel idle"
     * signal to gate keystroke injection (typing during boot
     * paint can confuse the OS). */
    [[nodiscard]] int framesIdle() const noexcept { return framesIdle_; }

    /* Run the dirty-cell sweep.  For each cell whose 8-byte glyph
     * changed since the previous flush, look up the new code in the
     * font map and (if different from the shadow) emit ANSI cursor +
     * UTF-8 char to out_ and append the char to history_. */
    void flushFrame();

    /* Drop all dirty markers and force a re-decode of the full screen
     * on the next flushFrame().  Use after loadState() or any other
     * coarse VRAM overwrite the hook didn't see byte-by-byte. */
    void invalidate() noexcept;

    /* ── Snapshot (for tests / static dumps) ─────────────────────────── */

    struct Snapshot {
        std::array<uint8_t, kCols * kRows> cells{};
        /* True if the cell at index `i` is XOR-inverted on the source
         * screen.  Rodionov ROSA Commander uses this for the
         * highlighted file entry; we resolve such cells to the
         * underlying glyph code and flag the inversion so the emit
         * path can wrap the cell with `\x1B[7m`…`\x1B[27m`. */
        std::array<bool, kCols * kRows> inverted{};

        /* Row `r` as a string with trailing blanks removed (KOI-8). */
        [[nodiscard]] std::string row(int r) const;
    };

    /* Take a snapshot of the current shadow state.  Equivalent to what
     * was emitted up to the most recent flushFrame(). */
    [[nodiscard]] Snapshot snapshot() const;

    /* Convert a single Snapshot cell code to its UTF-8 byte sequence.
     * Returns "█" for kUnknownGlyph, "©" for kCopyrightSign,
     * "▼"/"▲" for kArrowDown/kArrowUp, single ASCII char for printable
     * 0x20..0x7E, and the kKoi8Hi[] codepoint for any byte 0x80..0xFF.
     * Public so UI layers can render snapshots without re-implementing
     * the KOI-8 → Unicode table. */
    [[nodiscard]] static std::string utf8FromKoi8(uint8_t code);

private:
    void onVramWrite(uint16_t offset, uint8_t value);
    void rebuildFontIfNeeded();
    void buildFont(const uint8_t *rom, size_t romSize);

    [[nodiscard]] static uint64_t glyphKey(const uint8_t glyph[8]);
    [[nodiscard]] uint8_t lookup(uint64_t key) const;

    /* Like lookup() but also reports whether the resolution required
     * inverting the bitmap (Rodionov highlight).  `inverted=true`
     * means the underlying glyph was found via XOR fallback; the
     * emit path is expected to wrap the cell with reverse-video
     * SGR codes. */
    struct LookupResult { uint8_t code; bool inverted; };
    [[nodiscard]] LookupResult lookupWithInvert(uint64_t key) const;

    /* Pack the eight bytes of the (row, col) cell from VRAM into a
     * 64-bit glyph key. */
    [[nodiscard]] uint64_t readGlyphKey(int row, int col) const;

    void emitCellChange(int row, int col, uint8_t newCode, bool inverted);
    void emitAnsi(std::string_view s);
    void emitKoi8(uint8_t code);

    Emulator *emu_ = nullptr;
    FILE     *out_ = nullptr;
    std::string history_;

    std::unordered_map<uint64_t, uint8_t> glyphMap_;
    uint32_t                              fontRomCrc_ = 0;
    bool                                  fontBuilt_  = false;

    std::array<uint8_t, kCols * kRows> shadow_{};   /* current cell content */
    std::array<bool,    kCols * kRows> inverted_{}; /* parallel inversion flag */
    std::array<bool,    kCols * kRows> dirty_{};    /* changed since last flush */
    int  cursorRow_     = -1;                       /* last-emitted cell */
    int  cursorCol_     = -1;
    int  hostCursorRow_ = -1;                       /* where host cursor is now */
    int  hostCursorCol_ = -1;
    bool hostCursorVisible_ = false;                /* `\x1B[?25h` last sent */
    int  lastWriteRow_  = -1;                       /* most recent VRAM write */
    int  lastWriteCol_  = -1;
    int  framesIdle_    = 0;                        /* frames since last write */
    int  writesThisFlush_ = 0;                      /* byte writes since last flush */

    /* OS-side cursor — the cell where the kernel currently draws its
     * blinking `_`.  Detected on the fly: any cell that resolves to
     * '_' (KOI-8 0x5F) is treated as the cursor.  We suppress the
     * `_` glyph in the host stream and park the host terminal's
     * native cursor at this position instead, so:
     *   - the cursor blinks at the host's native rate (independent
     *     of emu speed, no more "super-fast blink in fast mode");
     *   - we don't double-render the cursor as glyph + host cursor.
     * When the OS moves the cursor (writes '_' to a different cell),
     * the previous cell is committed to host with whatever VRAM byte
     * the user actually typed there (which is usually the typed
     * character, not '_'). */
    int  osCursorRow_ = -1;
    int  osCursorCol_ = -1;
};

} /* namespace ms0515 */

#endif /* MS0515_VRAM_MIRROR_HPP */
