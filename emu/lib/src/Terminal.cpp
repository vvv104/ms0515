/*
 * Terminal.cpp — Output-only host-terminal mirror.
 *
 * Classification cascade for each delta:
 *
 *   no diff           → emit nothing.
 *   scroll-up by k    → emit `k` newlines, each followed by the
 *                       new-bottom row content.  k may be anything
 *                       up to kRows-1 — bursts of output between
 *                       samples often shift the screen by many lines
 *                       at once.  We require the preserved overlap
 *                       to contain real text so a near-empty screen
 *                       doesn't match itself trivially.
 *   incremental       → row-by-row append.  Each changed row is
 *                       either the host's current line (suffix
 *                       appended in place, or full-line rewrite if
 *                       the prefix changed) or strictly below it
 *                       (`\n`s to advance, then full content).  Any
 *                       backwards change rejects this strategy.
 *   redraw            → marker line + every non-blank row.  Used as
 *                       the fallback when neither scroll nor
 *                       incremental fits.
 *
 * The "host cursor row" is tracked in lastEmitRow_ — initialised to
 * the highest non-empty row of the very first snapshot we dump, then
 * updated by every emit path.  No ANSI cursor positioning: the host
 * terminal's natural top-down flow plus its own scroll buffer give
 * the user back the scrollback the MS-0515 itself does not have.
 */

#include <ms0515/Terminal.hpp>

#include "EmulatorInternal.hpp"   /* internal::vram / internal::rom */

#include <algorithm>
#include <bit>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace ms0515 {

namespace {

/* ── KOI-8R 0xC0..0xFF → UTF-8 Cyrillic (Russian-section table) ──────── */

const char *koi8rToUtf8(uint8_t code)
{
    static const char *table[64] = {
        /* 0xC0 */ "ю", "а", "б", "ц", "д", "е", "ф", "г",
        /* 0xC8 */ "х", "и", "й", "к", "л", "м", "н", "о",
        /* 0xD0 */ "п", "я", "р", "с", "т", "у", "ж", "в",
        /* 0xD8 */ "ь", "ы", "з", "ш", "э", "щ", "ч", "ъ",
        /* 0xE0 */ "Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г",
        /* 0xE8 */ "Х", "И", "Й", "К", "Л", "М", "Н", "О",
        /* 0xF0 */ "П", "Я", "Р", "С", "Т", "У", "Ж", "В",
        /* 0xF8 */ "Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "Ъ",
    };
    return code >= 0xC0 ? table[code - 0xC0] : nullptr;
}

/* UTF-8 encoding of █ (U+2588) — visible marker for kUnknownGlyph. */
constexpr char kUnknownGlyphUtf8[] = "\xE2\x96\x88";

/* UTF-8 encoding of © (U+00A9) — what kCopyrightSign decodes to. */
constexpr char kCopyrightUtf8[] = "\xC2\xA9";

/* Find the file offset of an 8-byte glyph bitmap in the ROM by visual
 * shape — i.e. an exact bitmap-pattern match.  We don't care which
 * character code the ROM stores at that position; the anchor's
 * fixed-position-within-its-font-table lets us derive the table base
 * by subtracting the anchor's index from the located offset.  This
 * frees the decoder from assuming any specific ROM revision: A and B
 * builds shift the font tables but keep glyph shapes identical. */
int findFontBase(std::span<const uint8_t> rom,
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

} /* anonymous namespace */

/* ── KOI-8 → UTF-8 emitters ──────────────────────────────────────────── */

void Terminal::putKoi8Char(FILE *f, uint8_t koi8)
{
    if (koi8 == kUnknownGlyph) {
        std::fputs(kUnknownGlyphUtf8, f);
    } else if (koi8 == kCopyrightSign) {
        std::fputs(kCopyrightUtf8, f);
    } else if (koi8 >= 0x20 && koi8 < 0x7F) {
        std::fputc(koi8, f);
    } else if (koi8 >= 0xC0) {
        const char *utf = koi8rToUtf8(koi8);
        std::fputs(utf ? utf : kUnknownGlyphUtf8, f);
    } else {
        std::fputc('.', f);
    }
}

void Terminal::appendKoi8Char(std::string &dst, uint8_t koi8)
{
    if (koi8 == kUnknownGlyph) {
        dst.append(kUnknownGlyphUtf8);
    } else if (koi8 == kCopyrightSign) {
        dst.append(kCopyrightUtf8);
    } else if (koi8 >= 0x20 && koi8 < 0x7F) {
        dst.push_back(static_cast<char>(koi8));
    } else if (koi8 >= 0xC0) {
        const char *utf = koi8rToUtf8(koi8);
        if (utf) dst.append(utf);
        else     dst.append(kUnknownGlyphUtf8);
    } else {
        dst.push_back('.');
    }
}

/* ── Snapshot::row ───────────────────────────────────────────────────── */

std::string Terminal::Snapshot::row(int r) const
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

/* ── Decode pipeline (font map + per-cell cache) ─────────────────────── */

uint64_t Terminal::glyphKey(const uint8_t glyph[8])
{
    uint64_t key = 0;
    for (int i = 0; i < 8; ++i)
        key |= static_cast<uint64_t>(glyph[i]) << (i * 8);
    return key;
}

uint64_t Terminal::readCell(std::span<const uint8_t> vram, int col, int row,
                            int bytesPerLine)
{
    uint8_t glyph[8];
    for (int y = 0; y < kGlyphH; ++y) {
        int offset = (row * kGlyphH + y) * bytesPerLine + col;
        glyph[y] = vram[offset];
    }
    return glyphKey(glyph);
}

uint64_t Terminal::readCellLores(std::span<const uint8_t> vram, int col, int row)
{
    /* In lores (320×200) each scan line is 40 words = 80 bytes.  Pixel
     * data lives in the low byte of each word (even offsets), so column
     * c reads byte offset `c*2`. */
    uint8_t glyph[8];
    for (int y = 0; y < kGlyphH; ++y) {
        int offset = (row * kGlyphH + y) * 80 + col * 2;
        glyph[y] = vram[offset];
    }
    return glyphKey(glyph);
}

uint8_t Terminal::lookup(uint64_t key) const
{
    auto it = glyphMap_.find(key);
    if (it != glyphMap_.end())
        return it->second;
    if (key == 0)
        return 0x20;  /* All-zero = blank */
    /* Sparse-pixel fallback.  Anything with fewer than 17 set pixels
     * (out of 64) is treated as blank — the OS scatters thin
     * horizontal-bar / leftover patterns into VRAM that scale to 1–2
     * barely-visible pixels at 640×200; emitting █ for them in
     * scrollback makes noise.  Denser patterns stay as kUnknownGlyph
     * (real visible bitmaps the OS draws but the ROM font doesn't
     * carry, e.g. ©). */
    if (std::popcount(key) < 17)
        return 0x20;
    return kUnknownGlyph;
}

void Terminal::buildFont(std::span<const uint8_t> rom)
{
    glyphMap_.clear();
    invalidateDecodeCache();

    /* Main-font anchor: visual shape of '0' (KOI-8 0x30). */
    static constexpr uint8_t kAnchorZero[8] = {
        0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
    };
    /* Alt-font anchor: visual shape of 'А' (KOI-8 0xE1).  Lives at
     * alt index 33 (= (0xE1 & 0x7F) - 0x40). */
    static constexpr uint8_t kAnchorCyrA[8] = {
        0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00,
    };
    static constexpr int kAnchorMainCode = 0x30;
    static constexpr int kAnchorAltIndex = 33;

    const int kMainFontFileOff = findFontBase(rom, kAnchorZero,
                                              kAnchorMainCode - 0x20);
    const int kAltFontFileOff  = findFontBase(rom, kAnchorCyrA,
                                              kAnchorAltIndex);

    const auto romSize = rom.size();

    /* Main font (KOI-8 0x20–0x7F).  Insert-if-absent so EARLIER codes
     * win on a glyph collision — '@' 0x40 and '`' 0x60 share a
     * bitmap in the ROM, but we want the printable ASCII letter to
     * be the canonical lookup result. */
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

    /* Cyrillic alt font (KOI-8 0xC0–0xFF → alt-table indices 0–63).
     * Add only patterns the main font hasn't already claimed so
     * printable ASCII stays canonical for ambiguous glyphs (e.g.
     * '_' 0x5F vs Ъ 0xFF, identical 8×8). */
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

    /* OS-drawn glyphs that aren't in either ROM font.  Each entry
     * was extracted from a real boot trace; insertion is `try_emplace`
     * so a ROM-supplied glyph with the same shape would still take
     * precedence. */
    static constexpr struct { uint64_t key; uint8_t code; } kCustomGlyphs[] = {
        /* © as drawn by the Rodionov 1992 OSA boot banner (popcount 26).
         * KOI-8R has no copyright sign, so we map it to kCopyrightSign
         * (Latin-1 0xA9) and let the emitters produce the UTF-8
         * sequence for ©. */
        { 0x3C4299A1A199423CULL, kCopyrightSign },
    };
    for (const auto &g : kCustomGlyphs)
        glyphMap_.try_emplace(g.key, g.code);
}

const Terminal::Snapshot &
Terminal::readScreen(std::span<const uint8_t> vram, bool hires)
{
    /* For each cell we re-extract the 8-byte glyph bitmap from VRAM
     * (cheap — eight byte loads + bit shifts) and compare its 64-bit
     * key against `cachedKeys_`.  Cells whose key matches keep the
     * decoded code from `cachedSnap_`; only mismatched cells go
     * through the unordered_map font lookup.  On a static screen the
     * inner branch collapses to a key compare per cell — exactly
     * what the per-frame sampling loop wants. */
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
            cachedKeys_[idx]       = key;
            cachedSnap_.cells[idx] = lookup(key);
        }
    }

    cachedCols_ = cols;
    cacheValid_ = true;
    return cachedSnap_;
}

/* ── Live-emulator entry points ──────────────────────────────────────── */

Terminal::Snapshot Terminal::decode(const Emulator &emu)
{
    /* Auto-rebuild the font map when the ROM changes.  CRC32 of 16 KB
     * is sub-microsecond and saves the frontend from threading a
     * "rom dirty" flag through loadRom / loadState. */
    const uint32_t crc = emu.romCrc32();
    if (!fontBuilt_ || crc != fontRomCrc_) {
        buildFont(internal::rom(emu));
        fontRomCrc_ = crc;
        fontBuilt_  = true;
    }
    return readScreen(internal::vram(emu), emu.isHires());
}

void Terminal::update(const Emulator &emu)
{
    feedSample(decode(emu));
}

Terminal::Terminal()
{
    shadow_.cells.fill(0x20);
    shadow_.cols = Terminal::kHiresCols;
}

void Terminal::reset() noexcept
{
    shadow_.cells.fill(0x20);
    shadow_.cols          = Terminal::kHiresCols;
    hasShadow_            = false;
    lastEmitRow_          = -1;
    lastEmittedLine_.clear();
    lastScreenStart_      = 0;
    hasLastForwardedSnap_ = false;
}

void Terminal::emitChar(char c)
{
    history_.push_back(c);
    if (out_) std::fputc(static_cast<unsigned char>(c), out_);
}

void Terminal::emitText(std::string_view s)
{
    history_.append(s);
    if (out_ && !s.empty())
        std::fwrite(s.data(), 1, s.size(), out_);
}

void Terminal::emitKoi8(uint8_t koi8)
{
    Terminal::appendKoi8Char(history_, koi8);
    if (out_) Terminal::putKoi8Char(out_, koi8);
}

/* UTF-8 byte cost of the same KOI-8 cell that `appendKoi8Char` would
 * have written.  Mirrors the case ladder there: kUnknownGlyph → █ (3),
 * kCopyrightSign → © (2), printable ASCII → 1, KOI-8R upper half (0xC0
 * .. 0xFF) → 2, anything else → '.' (1). */
static std::size_t utf8BytesForKoi8(uint8_t koi8)
{
    if (koi8 == Terminal::kUnknownGlyph)            return 3;
    if (koi8 == Terminal::kCopyrightSign)           return 2;
    if (koi8 >= 0x20 && koi8 <  0x7F)               return 1;
    if (koi8 >= 0xC0)                               return 2;
    return 1;
}

/* Erase one KOI-8 cell from the tail of the output: pop its UTF-8
 * bytes off `history_` (so ImGui sees a clean shorter string — it
 * does not interpret control codes), and write `\b \b` to the host
 * FILE* (which a real terminal does interpret as cursor-back / blank
 * / cursor-back).  Caller passes the KOI-8 code that was written so
 * the UTF-8 byte count matches what `appendKoi8Char` produced. */
void Terminal::emitBackspace(uint8_t koi8)
{
    const std::size_t n = utf8BytesForKoi8(koi8);
    if (history_.size() >= n)
        history_.resize(history_.size() - n);
    if (out_) std::fputs("\b \b", out_);
}

std::string Terminal::trimmedRow(const Snapshot &s, int r) const
{
    if (r < 0 || r >= Terminal::kRows) return {};
    const int cols = s.cols;
    std::string text;
    text.reserve(static_cast<size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        uint8_t code = s.cells[r * Terminal::kHiresCols + c];
        /* Any character in transparentChars_ collapses to a space —
         * cursor blink and unknown-glyph placeholders should not
         * register as content for diff classification. */
        if (transparentChars_.find(static_cast<char>(code)) != std::string::npos)
            code = 0x20;
        text.push_back(static_cast<char>(code));
    }
    while (!text.empty() && static_cast<uint8_t>(text.back()) == 0x20)
        text.pop_back();
    return text;
}

int Terminal::lastNonEmptyRow(const Snapshot &s) const
{
    for (int r = Terminal::kRows - 1; r >= 0; --r) {
        if (!trimmedRow(s, r).empty()) return r;
    }
    return -1;
}

int Terminal::detectScrollUp(const Snapshot &cur) const
{
    if (!hasShadow_)              return 0;
    if (shadow_.cols != cur.cols) return 0;

    /* Try every plausible scroll amount.  k=1 is the dominant case
     * but bursts of output (DIR listings, long boot messages) can
     * shift the visible window by many rows in a single 200-ms
     * sample window. */
    for (int k = 1; k < Terminal::kRows; ++k) {
        bool ok        = true;
        int  matched   = 0;
        for (int i = 0; i + k < Terminal::kRows; ++i) {
            const auto sh = trimmedRow(shadow_, i + k);
            const auto cu = trimmedRow(cur, i);
            if (sh != cu) { ok = false; break; }
            if (!sh.empty()) ++matched;
        }
        /* Reject low-content matches.  With <2 non-blank preserved
         * rows we can't really tell scroll from "different screen
         * that happens to share one row" — the multi-row append /
         * redraw paths handle that case more honestly. */
        if (ok && matched >= 2) return k;
    }
    return 0;
}

bool Terminal::isUnchanged(const Snapshot &cur) const
{
    if (!hasShadow_)              return false;
    if (shadow_.cols != cur.cols) return false;
    for (int r = 0; r < Terminal::kRows; ++r) {
        if (trimmedRow(shadow_, r) != trimmedRow(cur, r))
            return false;
    }
    return true;
}

void Terminal::emitRowText(const Snapshot &snap, int row)
{
    const std::string text = trimmedRow(snap, row);
    for (uint8_t c : text)
        emitKoi8(c);
}

void Terminal::emitRowLine(const Snapshot &snap, int row)
{
    emitRowText(snap, row);
    emitChar('\n');
}

bool Terminal::tryEmitIncremental(const Snapshot &cur)
{
    /* Two-pass: build a list of operations first so we can validate
     * the plan before any output is committed; then replay it
     * through the real emit helpers.  The split also routes content
     * bytes through emitKoi8 (which handles KOI-8R → UTF-8) instead
     * of emitChar (raw byte): writing a Cyrillic KOI-8 code as a
     * single byte produced invalid UTF-8 in history_, which is what
     * the user saw rendered as U+FFFD (`�`) in the Terminal window. */
    struct Op {
        enum Kind { Newline, Content, Backspace };
        Kind     kind;
        uint8_t  koi8;   /* Content: byte to push.  Backspace: byte
                          * being erased — drives the UTF-8 byte
                          * count when popping it off history_. */
    };

    int cursorRow = lastEmitRow_;
    int cursorCol = cursorRow >= 0
                  ? static_cast<int>(trimmedRow(shadow_, cursorRow).size())
                  : 0;

    std::vector<Op> plan;

    for (int r = 0; r < Terminal::kRows; ++r) {
        const std::string oldText = trimmedRow(shadow_, r);
        const std::string newText = trimmedRow(cur, r);
        if (oldText == newText) continue;

        if (r > cursorRow) {
            /* Forward jump.  Advance host cursor with `\n`s, then
             * write the full new row content.  oldText must be empty
             * — this row hasn't been touched before, so we only
             * accept the "fresh row" pattern. */
            if (!oldText.empty()) return false;
            const int nNl = (cursorRow < 0 ? r : r - cursorRow);
            for (int i = 0; i < nNl; ++i)
                plan.push_back({Op::Newline, 0});
            for (uint8_t c : newText)
                plan.push_back({Op::Content, c});
            cursorRow = r;
            cursorCol = static_cast<int>(newText.size());
        } else if (r == cursorRow) {
            /* Same row as host cursor.  Three sub-cases:
             *
             *   a. newText is a prefix-extension of oldText (suffix
             *      typed) — emit only the new tail.
             *   b. newText is a strict prefix of oldText (backspace
             *      / DEL) — back up over the removed cells.
             *   c. neither (mid-line edit, e.g. user retyped after
             *      backspacing) — back up over the divergent suffix
             *      of oldText, then emit the divergent suffix of
             *      newText.
             *
             * The unified handling: find the common prefix, then
             * issue Backspace ops for the trailing oldText cells and
             * Content ops for the trailing newText cells.  Case (a)
             * collapses to "no backspaces, append-only"; case (b) to
             * "backspaces, no content"; (c) is the general form.
             *
             * Using emitBackspace instead of the older `\r`/space
             * /`\r` rewrite keeps history_ valid UTF-8 (no embedded
             * `\r`) so the ImGui scrollback view doesn't render
             * stray glyphs at the rewrite points. */
            std::size_t common = 0;
            while (common < oldText.size() && common < newText.size() &&
                   oldText[common] == newText[common])
                ++common;
            for (std::size_t i = oldText.size(); i > common; --i)
                plan.push_back({Op::Backspace,
                                static_cast<uint8_t>(oldText[i - 1])});
            for (std::size_t i = common; i < newText.size(); ++i)
                plan.push_back({Op::Content,
                                static_cast<uint8_t>(newText[i])});
            cursorCol = static_cast<int>(newText.size());
        } else {
            /* r < cursorRow — we'd have to scroll the host
             * backwards, which `\n`-only output cannot do.  Bail. */
            return false;
        }
    }

    /* Plan validated — execute through the real emit helpers.
     * Newlines go through emitChar; content bytes go through
     * emitKoi8 so KOI-8R Cyrillic codes turn into proper multi-byte
     * UTF-8 in history_; backspaces pop the same UTF-8 bytes off
     * the tail and write `\b \b` to the host FILE*. */
    for (const Op &op : plan) {
        switch (op.kind) {
            case Op::Newline:   emitChar('\n');         break;
            case Op::Content:   emitKoi8(op.koi8);      break;
            case Op::Backspace: emitBackspace(op.koi8); break;
        }
    }

    if (cursorRow > lastEmitRow_)
        lastEmitRow_ = cursorRow;
    /* Track the line content the host cursor is parked on, so the
     * dedup fallback can detect suffix extensions on it. */
    if (cursorRow >= 0)
        lastEmittedLine_ = trimmedRow(cur, cursorRow);
    if (out_) std::fflush(out_);
    return true;
}

void Terminal::emitDedup(const Snapshot &cur)
{
    /* Build the set of trimmed lines visible in shadow — these have
     * already been emitted and shouldn't appear again on the wire. */
    std::unordered_set<std::string> shadowLines;
    for (int r = 0; r < Terminal::kRows; ++r) {
        auto t = trimmedRow(shadow_, r);
        if (!t.empty()) shadowLines.insert(std::move(t));
    }

    /* First pass — collect the non-blank rows of `cur` along with
     * their original row positions, and classify each as preserved
     * (already in shadow) or new.  Two patterns trigger a "full
     * screen redraw" treatment:
     *
     *   - Re-layout: a preserved row appears AFTER a new row in walk
     *     order, meaning the OS slipped a new line *above* something
     *     we'd already emitted.  Linear `\n`-only output can't
     *     represent that.
     *
     *   - All-new: not a single row of `cur` overlaps with the
     *     previous shadow (the OS cleared and reprinted from
     *     scratch — DIR, CLS, a different program taking over).
     *     Without the marker the new content would just append to
     *     scrollback with no visual break; with it, the UI scrolls
     *     the new screen to the top of the viewport so it looks
     *     like the OS terminal redraw it actually was.
     *
     * We retain `row.idx` so the emitter can preserve the OS's row
     * spacing (blank rows between content) — without it scrollback
     * would squash three lines of "© 1992", blank, ".SET TT QUIET"
     * into "© 1992\n.SET TT QUIET", losing the visual layout. */
    struct Row { int idx; std::string text; bool preserved; };
    std::vector<Row> rows;
    rows.reserve(Terminal::kRows);
    bool sawNew      = false;
    bool sawPreserved = false;
    bool isRelayout  = false;
    for (int r = 0; r < Terminal::kRows; ++r) {
        auto t = trimmedRow(cur, r);
        if (t.empty()) continue;
        const bool preserved = shadowLines.count(t) > 0;
        if (preserved && sawNew)  isRelayout  = true;
        if (preserved)            sawPreserved = true;
        if (!preserved)           sawNew       = true;
        rows.push_back({r, std::move(t), preserved});
    }
    /* Decide whether the snapshot deserves "fresh screen" treatment
     * (blank-line separator + lastScreenStart_ advance, so the UI
     * scrolls the new content to viewport top) or should land as a
     * plain append below the existing scrollback.
     *
     *   - cur has ≥3 non-blank rows      → big new screen (POST,
     *                                       boot banner, DIR output);
     *                                       redraw.
     *   - shadow had ≥3 non-blank rows   → the OS just wiped a full
     *                                       screen and replaced it
     *                                       with a small prompt
     *                                       (e.g. "Время [чч:мм]?"
     *                                       after DIR); redraw.
     *   - both are small (e.g. "© 1992
     *     Родионов С.А." after "НГМД
     *     готов...")                    → append.  Keep the
     *                                       previous anchor so the
     *                                       user's view of "НГМД
     *                                       готов..." stays pinned
     *                                       at the top until enough
     *                                       content accumulates
     *                                       below it. */
    int shadowNonBlank = 0;
    for (int r = 0; r < Terminal::kRows; ++r)
        if (!trimmedRow(shadow_, r).empty()) ++shadowNonBlank;
    const bool allNew = !rows.empty() && !sawPreserved
                     && (rows.size() >= 3 || shadowNonBlank >= 3);

    if (isRelayout || allNew) {
        /* Reprint the current screen so scrollback shows the layout
         * the way the OS sees it.  A clearly-marked separator line
         * sits between the previous on-wire state and the new
         * screen so a user scrolling back through scrollback can
         * see exactly where each redraw happened.  lastScreenStart_
         * marks the byte offset of the new screen's first line so
         * the UI can scroll it to viewport top — the separator
         * lands one line above (just out of view).  Gaps between
         * non-blank rows are reproduced via extra newlines so the
         * OS's row spacing is preserved. */
        /* 80-column separator (screen width) with " screen redrawn "
         * centred — 32 dashes + 16-char label + 32 dashes = 80. */
        emitText("\n"
                 "-------------------------------- screen redrawn "
                 "--------------------------------\n");
        lastScreenStart_ = history_.size();
        /* Reproduce the OS row layout faithfully: emit row.idx
         * newlines before the first non-blank row's content (so any
         * blank rows above it on the OS screen become blank lines
         * in scrollback), then row.idx-prevIdx newlines between
         * subsequent rows.  Without this, blank rows at the top of
         * a freshly-drawn screen (e.g. POST starting with three
         * empty rows above the "Электроника" banner) get collapsed
         * and the visual layout differs from what the OS shows. */
        int prevIdx = 0;
        bool first = true;
        for (const auto &row : rows) {
            const int gap = first ? row.idx : (row.idx - prevIdx);
            for (int i = 0; i < gap; ++i) emitChar('\n');
            for (uint8_t c : row.text) emitKoi8(c);
            prevIdx = row.idx;
            first = false;
        }
        if (!rows.empty())
            lastEmittedLine_ = rows.back().text;
    } else {
        /* Linear dedup: emit only the new lines.  Suffix extension
         * (the new line starts with the last one we put on the
         * wire) writes just the appended tail; otherwise the new
         * line lands at the host cursor's expected row gap.  Gaps
         * between cur rows (preserved or absent) are reproduced via
         * extra newlines so blank-line separators in the OS's
         * layout reach scrollback intact. */
        int lastIdx = -1;
        for (const auto &row : rows) {
            if (row.preserved) {
                lastIdx = row.idx;
                continue;
            }
            const bool suffixExt = !lastEmittedLine_.empty() &&
                row.text.size() > lastEmittedLine_.size() &&
                std::memcmp(row.text.data(), lastEmittedLine_.data(),
                            lastEmittedLine_.size()) == 0;
            if (suffixExt) {
                for (size_t i = lastEmittedLine_.size();
                     i < row.text.size(); ++i)
                    emitKoi8(static_cast<uint8_t>(row.text[i]));
            } else {
                const int gap = lastIdx >= 0 ? row.idx - lastIdx : 1;
                for (int i = 0; i < gap; ++i) emitChar('\n');
                for (uint8_t c : row.text) emitKoi8(c);
            }
            lastIdx = row.idx;
            lastEmittedLine_ = row.text;
        }
    }

    if (out_) std::fflush(out_);
    shadow_      = cur;
    lastEmitRow_ = lastNonEmptyRow(cur);
}

/*
 * Helper: emit the visible rows of `snap` preserving the OS's row
 * spacing.  Leading and trailing blank rows are dropped (so the
 * scrollback isn't padded with a screenful of empty lines), but
 * blank rows BETWEEN content rows become blank lines on the wire
 * — every row from the first non-empty to the last non-empty
 * contributes either its text or a `\n` for the spacing gap.  Used
 * by the initial-dump path and the redraw-marker path so that
 * lastEmitRow_ ends up at the row of the last emitted character. */
static void emitRowsPreservingGaps(Terminal &t, const Terminal::Snapshot &snap,
                                   auto &&rowText, auto &&emitNl,
                                   auto &&emitText)
{
    int first = -1, last = -1;
    for (int r = 0; r < Terminal::kRows; ++r) {
        if (!rowText(snap, r).empty()) {
            if (first < 0) first = r;
            last = r;
        }
    }
    if (first < 0) return;   /* all-blank — nothing to emit */

    emitText(rowText(snap, first));
    int prev = first;
    for (int r = first + 1; r <= last; ++r) {
        for (int i = 0; i < r - prev; ++i) emitNl();
        const auto text = rowText(snap, r);
        if (!text.empty()) emitText(text);
        prev = r;
    }
    (void)t;
}

void Terminal::feedSample(const Terminal::Snapshot &snap)
{
    /* Gate 1: clean — no unknown-glyph cells (mid-bitmap-rewrite
     * produces partial keys that decode as kUnknownGlyph). */
    bool clean = true;
    for (uint8_t code : snap.cells) {
        if (code == Terminal::kUnknownGlyph) {
            clean = false;
            break;
        }
    }

    /* Gate 2: progressing — no row in `snap` is a strict-subset
     * partial copy of *any* row in the last forwarded snap.
     * Mid-scroll routines copy content from one row into another
     * cell-by-cell; the intermediate state has cur.row[R] looking
     * like a partial copy of lastForwardedSnap_.row[R'] for some R'.
     * Strict subset = cur's non-blanks are at the same positions
     * as ref's with the same values, and cur has fewer non-blanks.
     * Reject those — they're mid-scroll-copy garbage.
     *
     * Stability across two consecutive samples was tried as a third
     * gate but rejected almost every mid-DIR frame at per-emu-frame
     * sampling (consecutive samples differ by one printed line).
     * Letting non-stable but clean+progressing samples through is
     * what allows files 1-2 of a fast DIR to reach scrollback before
     * scrolling pushes them off the OS screen. */
    /* Treat cursor cells (any character listed in transparentChars_)
     * as blank when comparing — otherwise cursor blink (cell
     * toggling between '_' and ' ') makes a row look like a strict
     * subset of itself-with-cursor and trips the progressing gate
     * every time the user is at a prompt.  All forwards get
     * rejected, and the next clean state we eventually accept is
     * far past the prompt — which is exactly what produced the
     * spurious "screen redrawn" separators between successive
     * commands. */
    auto stripTransparent = [&](uint8_t c) -> uint8_t {
        return transparentChars_.find(static_cast<char>(c))
                 != std::string::npos ? uint8_t(0x20) : c;
    };

    bool progressing = true;
    if (clean && hasLastForwardedSnap_
     && lastForwardedSnap_.cols == snap.cols) {
        for (int r = 0; r < Terminal::kRows && progressing; ++r) {
            const uint8_t *cur = &snap.cells[r * Terminal::kHiresCols];
            int curNonBlank = 0;
            for (int c = 0; c < snap.cols; ++c)
                if (stripTransparent(cur[c]) != 0x20) ++curNonBlank;
            if (curNonBlank < 3) continue;
            for (int rr = 0; rr < Terminal::kRows; ++rr) {
                /* Skip the same-row comparison: a row shrinking
                 * relative to its own previous content is normal
                 * progress (backspace / DEL / end-of-line clear),
                 * not a mid-scroll-copy.  The mid-scroll signature
                 * we want to catch here is `cur.row[R]` looking
                 * like a partial copy of `lastForwardedSnap_.row[R']`
                 * where R' ≠ R — that's the OS copying row R+1 down
                 * onto row R cell-by-cell.  Keeping rr == r in the
                 * loop made every backspace under a 3-char-or-more
                 * row register as "subset of itself shrunk" and
                 * dropped the snap, so backspaces only became
                 * visible once the row fell under the curNonBlank<3
                 * noise floor — exactly the symptom the user
                 * reported. */
                if (rr == r) continue;
                const uint8_t *ref = &lastForwardedSnap_.cells[
                    rr * Terminal::kHiresCols];
                int refNonBlank = 0;
                bool subset = true;
                for (int c = 0; c < snap.cols; ++c) {
                    const uint8_t curC = stripTransparent(cur[c]);
                    const uint8_t refC = stripTransparent(ref[c]);
                    if (refC != 0x20) ++refNonBlank;
                    if (curC != 0x20 && refC != curC) {
                        subset = false;
                        break;
                    }
                }
                if (subset && curNonBlank < refNonBlank) {
                    progressing = false;
                    break;
                }
            }
        }
    }

    /* Gate 3: no-adjacent-duplicate — strict mid-scroll-copy
     * signature: cur.row[R] and cur.row[R+1] are both equal to
     * shadow.row[R+1].  In a mid-scroll-up, the OS copies row R+1
     * down onto row R cell-by-cell and hasn't yet cleared row R+1
     * (the original), so both rows hold the row-R+1 content for a
     * few sample cycles.  If we let it through, detectScrollUp
     * would happily match (cur[i] == shadow[i+1] holds) and emit
     * the bottom row as new content — except it's the SAME row
     * already at row R, producing the "MORDA .SCR / MORDA .SCR"
     * dup the gate was added for.
     *
     * The earlier formulation rejected ANY two consecutive non-blank
     * rows with matching trimmed content, which false-positives on
     * legitimate runs of identical prompts ("." Enter "." Enter
     * "." Enter…) — the OS leaves a column of dots on screen and
     * we'd reject every snapshot, so the dots stayed un-emitted
     * until enough scrolling broke the streak.  Cross-checking
     * against shadow distinguishes the two cases: a mid-scroll is
     * the only one where the duplicated content was already at
     * row R+1 in the previous accepted sample. */
    bool noAdjacentDup = true;
    if (clean && progressing && hasShadow_) {
        for (int r = 0; r + 1 < Terminal::kRows; ++r) {
            const auto a = trimmedRow(snap, r);
            if (a.empty()) continue;
            if (trimmedRow(snap, r + 1) != a) continue;
            /* Two consecutive non-blank rows in cur with identical
             * trimmed content.  Mid-scroll-copy signature: cur row R
             * CHANGED (the OS just copied content `a` from row R+1
             * down onto it; previously row R held something else),
             * AND cur row R+1 PRESERVED (still holds the original
             * `a` because the OS hasn't cleared it yet).  In both
             * other directions — both preserved (a stable column of
             * identical prompts) or both changed (a fresh emit of
             * two identical lines) — the duplication is a real
             * intentional layout, not a transient. */
            if (trimmedRow(shadow_, r) != a
             && trimmedRow(shadow_, r + 1) == a) {
                noAdjacentDup = false;
                break;
            }
        }
    }

    if (clean && progressing && noAdjacentDup) {
        update(snap);
        lastForwardedSnap_    = snap;
        hasLastForwardedSnap_ = true;
    }
}

void Terminal::update(const Terminal::Snapshot &snap)
{
    /* Drop snapshots dominated by unknown-glyph cells.  At cold-boot
     * VRAM is full of 0xFF bytes — every cell decodes to
     * kUnknownGlyph and would otherwise land in scrollback as 25
     * lines of solid █.  Diag run on Rodionov OSA-B confirmed this
     * (frames 30–60 were entirely full-block).  A real OS screen
     * has at most a handful of unknowns (custom-bitmap glyphs the
     * font map can't decode, e.g. ©), so a threshold of >100 cleanly
     * separates "uninitialised VRAM" from "OS text with a few
     * unknown glyphs". */
    int unknownCount = 0;
    for (uint8_t code : snap.cells)
        if (code == Terminal::kUnknownGlyph) ++unknownCount;
    if (unknownCount > 100)
        return;

    /* Drop snapshots that have any single row peppered with multiple
     * unknown-glyph cells.  Real OS-drawn lines are clean ASCII +
     * Cyrillic with at most one or two custom glyphs (e.g. ©), so a
     * row with 3+ unknown cells is the signature of a mid-scroll /
     * mid-redraw moment where the OS copied some cells of a row but
     * not others — sampling caught the row half-rewritten with stale
     * pixel patterns from the previous content.  Forwarding those
     * leaks "█    .S█        █-0 -██"-style garbage into scrollback
     * AND triggers a spurious redraw separator (the corrupted line
     * has no match in shadow → isRelayout fires when subsequent
     * preserved rows follow it). */
    for (int r = 0; r < Terminal::kRows; ++r) {
        int rowUnknowns = 0;
        for (int c = 0; c < snap.cols; ++c) {
            if (snap.cells[r * Terminal::kHiresCols + c]
                == Terminal::kUnknownGlyph)
                ++rowUnknowns;
        }
        if (rowUnknowns >= 3)
            return;
    }

    /* Drop fully-blank snapshots.  When the OS transitions between
     * screens it clears VRAM mid-redraw — a snapshot landing in
     * that window has every row blank.  Letting it through would
     * overwrite shadow_ with the cleared state, and the very next
     * non-empty snapshot (e.g. "НГМД готов...") would see an
     * empty shadow → allNew fails the (shadow≥3) check → no
     * redraw fires → the previous anchor remains in force, and
     * auto-scroll-to-tail eventually pushes the new "first line"
     * off the top of the viewport.  Skipping the blank moment
     * preserves the previous shadow so the next emit correctly
     * triggers the screen-clear-and-replace path. */
    bool anyContent = false;
    for (int r = 0; r < Terminal::kRows; ++r) {
        if (!trimmedRow(snap, r).empty()) { anyContent = true; break; }
    }
    if (!anyContent)
        return;

    /* First frame: dump the current visible content, preserving the
     * OS's row spacing — leading and trailing blank rows are
     * skipped, but blanks BETWEEN content rows reach scrollback as
     * `\n`-only lines.  No trailing newline: lastEmitRow_ tracks
     * the row of the last emitted character so the host cursor
     * sits at the end of the last non-empty row's content. */
    if (!hasShadow_) {
        /* The initial dump is the first "screen" the UI should
         * anchor against. */
        lastScreenStart_ = history_.size();
        emitRowsPreservingGaps(
            *this, snap,
            [&](const Snapshot &s, int r){ return trimmedRow(s, r); },
            [&]{ emitChar('\n'); },
            [&](const std::string &t){
                for (uint8_t c : t) emitKoi8(c);
            });
        if (out_) std::fflush(out_);
        shadow_           = snap;
        hasShadow_        = true;
        lastEmitRow_      = lastNonEmptyRow(snap);
        lastEmittedLine_  = lastEmitRow_ >= 0
                          ? trimmedRow(snap, lastEmitRow_)
                          : std::string{};
        return;
    }

    if (isUnchanged(snap))
        return;

    /* Try scroll first — it's the cleanest representation when it
     * fits, and the host's native scrollback gets to keep the
     * lines that fell off the top.  Each new-bottom row is emitted
     * with a leading `\n` so the host cursor advances onto a fresh
     * line before the row's content lands. */
    if (int k = detectScrollUp(snap); k > 0) {
        for (int i = 0; i < k; ++i) {
            emitChar('\n');
            emitRowText(snap, Terminal::kRows - k + i);
        }
        if (out_) std::fflush(out_);
        shadow_           = snap;
        lastEmitRow_      = Terminal::kRows - 1;
        lastEmittedLine_  = trimmedRow(snap, Terminal::kRows - 1);
        return;
    }

    /* Try the row-by-row incremental plan: handles single-row
     * appends, multi-row "OS just printed three lines below the
     * prompt" bursts, and in-place line edits. */
    if (tryEmitIncremental(snap)) {
        shadow_ = snap;
        return;
    }

    /* Fallback: re-layout, partial scroll with suffix extension, or
     * any other change pattern the incremental plan can't express
     * forward-only.  Set-diff emits only the lines that aren't
     * already on the wire — no `[--- screen redrawn ---]` marker,
     * no duplication of preserved content. */
    emitDedup(snap);
}

} /* namespace ms0515 */
