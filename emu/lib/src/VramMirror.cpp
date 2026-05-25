/*
 * VramMirror.cpp — hook-driven 80×25 hires text mirror with ANSI output.
 */

#include <ms0515/VramMirror.hpp>

#include "EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/memory.h>
}

#include <bit>
#include <cstring>
#include <cstdio>

namespace ms0515 {

namespace {

/* Same KOI-8R high-half table that emu/cli/src/Koi8.cpp ships — kept
 * inline here so VramMirror doesn't depend on the CLI module.  Entry 0
 * corresponds to byte 0x80. */
constexpr uint32_t kKoi8Hi[128] = {
    0x2500,0x2502,0x250C,0x2510,0x2514,0x2518,0x251C,0x2524,
    0x252C,0x2534,0x253C,0x2580,0x2584,0x2588,0x258C,0x2590,
    0x2591,0x2592,0x2593,0x2320,0x25A0,0x2219,0x221A,0x2248,
    0x2264,0x2265,0x00A0,0x2321,0x00B0,0x00B2,0x00B7,0x00F7,
    0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,
    0x2557,0x2558,0x2559,0x255A,0x255B,0x255C,0x255D,0x255E,
    0x255F,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
    0x2566,0x2567,0x2568,0x2569,0x256A,0x256B,0x256C,0x00A9,
    0x044E,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
    0x0445,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,
    0x043F,0x044F,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
    0x044C,0x044B,0x0437,0x0448,0x044D,0x0449,0x0447,0x044A,
    0x042E,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
    0x0425,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,
    0x041F,0x042F,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
    0x042C,0x042B,0x0417,0x0428,0x042D,0x0429,0x0427,0x042A,
};

std::string encodeUtf8(uint32_t cp)
{
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

/* Find the file offset of an 8-byte glyph bitmap in the ROM by visual
 * shape — same trick Terminal::findFontBase uses (anchor-by-bitmap,
 * derive table base by subtracting the anchor's known index).  Picked
 * pair: '0' for the main font (KOI-8 0x30), 'А' for the alt font
 * (KOI-8 0xE1, alt-table index 33). */
int findFontBase(const uint8_t *rom, size_t romSize,
                 const uint8_t (&anchor)[8], int anchorIndex)
{
    if (romSize < 8) return -1;
    const size_t end = romSize - 8;
    for (size_t off = 0; off <= end; ++off) {
        if (std::memcmp(rom + off, anchor, 8) == 0) {
            const int base = static_cast<int>(off) - anchorIndex * 8;
            if (base >= 0)
                return base;
        }
    }
    return -1;
}

}  /* anonymous namespace */

VramMirror::VramMirror()
{
    shadow_.fill(0x20);
}

VramMirror::~VramMirror()
{
    detach();
}

void VramMirror::attach(Emulator &emu)
{
    if (emu_ != nullptr) detach();
    emu_ = &emu;
    /* Don't mark everything dirty here — the hook is now live, so any
     * subsequent VRAM write the CPU does will flag the right cell.
     * For a fresh-reset emu the shadow's all-blanks state matches the
     * zeroed VRAM and the first flushFrame() emits nothing.
     * After loadState() or any other coarse rewrite that bypasses the
     * hook, the caller is responsible for invalidate(). */
    emu.setVramWriteCallback(
        [this](uint16_t offset, uint8_t value) { onVramWrite(offset, value); });
}

void VramMirror::detach()
{
    if (emu_ == nullptr) return;
    emu_->setVramWriteCallback({});
    emu_ = nullptr;
}

void VramMirror::invalidate() noexcept
{
    dirty_.fill(true);
    fontBuilt_     = false;   /* force font rebuild on next flush */
    hostCursorRow_ = -1;
    hostCursorCol_ = -1;
}

/* Map a VRAM byte offset to the (row, col) cell it belongs to in the
 * 80×25 hires text layout.  Each scanline = 80 bytes; 8 scanlines per
 * cell row; 25 cell rows total = 200 scanlines = 16000 bytes used,
 * with the upper 384 bytes of the 16 KB VRAM unused for text. */
void VramMirror::onVramWrite(uint16_t offset, uint8_t /*value*/)
{
    if (offset >= 80 * 8 * 25) return;          /* past the text area */
    const int scanline = offset / 80;
    const int col      = offset % 80;
    const int row      = scanline / kGlyphH;
    if (row >= kRows || col >= kCols) return;
    dirty_[row * kCols + col] = true;
    lastWriteRow_     = row;
    lastWriteCol_     = col;
    ++writesThisFlush_;
}

uint64_t VramMirror::glyphKey(const uint8_t glyph[8])
{
    uint64_t key = 0;
    for (int i = 0; i < 8; ++i)
        key |= static_cast<uint64_t>(glyph[i]) << (i * 8);
    return key;
}

uint64_t VramMirror::readGlyphKey(int row, int col) const
{
    uint8_t glyph[8];
    auto vram = internal::vram(*emu_);
    for (int y = 0; y < kGlyphH; ++y) {
        int off = (row * kGlyphH + y) * 80 + col;
        glyph[y] = vram[static_cast<size_t>(off)];
    }
    return glyphKey(glyph);
}

uint8_t VramMirror::lookup(uint64_t key) const
{
    auto it = glyphMap_.find(key);
    if (it != glyphMap_.end()) return it->second;
    if (key == 0) return 0x20;                  /* all-zero = blank */
    /* Sparse-pixel fallback: <17 of 64 set pixels = visual noise = blank.
     * Threshold and reasoning copied from Terminal::lookup — the
     * OS scatters thin 1-2 pixel leftover patterns into otherwise
     * blank cells; emitting █ for those would mangle scrollback. */
    if (std::popcount(key) < 17) return 0x20;
    return kUnknownGlyph;
}

void VramMirror::rebuildFontIfNeeded()
{
    if (emu_ == nullptr) return;
    const uint32_t crc = emu_->romCrc32();
    if (fontBuilt_ && crc == fontRomCrc_) return;
    auto rom = internal::rom(*emu_);
    buildFont(rom.data(), rom.size());
    fontRomCrc_ = crc;
    fontBuilt_  = true;
}

void VramMirror::buildFont(const uint8_t *rom, size_t romSize)
{
    glyphMap_.clear();

    /* '0' (KOI-8 0x30) — main font visual anchor. */
    static constexpr uint8_t kAnchorZero[8] = {
        0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
    };
    /* 'А' (KOI-8 0xE1) — alt-font anchor at alt-table index 33. */
    static constexpr uint8_t kAnchorCyrA[8] = {
        0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00,
    };
    static constexpr int kAnchorMainCode = 0x30;
    static constexpr int kAnchorAltIndex = 33;

    const int mainBase = findFontBase(rom, romSize, kAnchorZero,
                                      kAnchorMainCode - 0x20);
    const int altBase  = findFontBase(rom, romSize, kAnchorCyrA,
                                      kAnchorAltIndex);

    /* Main font: KOI-8 0x20..0x7F.  Insert-if-absent so EARLIER codes
     * win on a shape collision — '@' 0x40 and '`' 0x60 sometimes share
     * a glyph in the ROM, but the printable ASCII code is what we
     * want lookups to return. */
    if (mainBase >= 0) {
        for (int code = 0x20; code < 0x80; ++code) {
            int off = mainBase + (code - 0x20) * 8;
            if (off + 8 > static_cast<int>(romSize)) break;
            uint64_t key = glyphKey(rom + off);
            if (key != 0 || code == 0x20)
                glyphMap_.emplace(key, static_cast<uint8_t>(code));
        }
    }

    /* Cyrillic alt font: KOI-8 0xC0..0xFF mapped to alt-table indices.
     * Only fills shapes the main font hasn't already claimed. */
    if (altBase >= 0) {
        for (int code = 0xC0; code <= 0xFF; ++code) {
            int glyph_idx = (code & 0x7F) - 0x40;
            int off = altBase + glyph_idx * 8;
            if (off < 0 || off + 8 > static_cast<int>(romSize)) continue;
            uint64_t key = glyphKey(rom + off);
            if (key != 0) glyphMap_.emplace(key, static_cast<uint8_t>(code));
        }
    }

    /* OS-drawn glyphs not in either ROM font.  Each extracted from a
     * real boot trace; try_emplace so a ROM glyph with the same shape
     * still wins. */
    static constexpr struct { uint64_t key; uint8_t code; } kCustomGlyphs[] = {
        /* © as drawn by the Rodionov 1992 OSA boot banner. */
        { 0x3C4299A1A199423CULL, kCopyrightSign },
    };
    for (const auto &g : kCustomGlyphs)
        glyphMap_.try_emplace(g.key, g.code);
}

std::string VramMirror::utf8FromKoi8(uint8_t code)
{
    if (code == kUnknownGlyph) return "\xE2\x96\x88";       /* █ */
    if (code == kCopyrightSign) return "\xC2\xA9";          /* © */
    if (code >= 0x20 && code < 0x7F)
        return std::string(1, static_cast<char>(code));
    if (code >= 0x80) return encodeUtf8(kKoi8Hi[code - 0x80]);
    return ".";
}

void VramMirror::emitAnsi(std::string_view s)
{
    if (out_) std::fwrite(s.data(), 1, s.size(), out_);
}

void VramMirror::emitKoi8(uint8_t code)
{
    auto utf = utf8FromKoi8(code);
    history_.append(utf);
    if (out_) std::fwrite(utf.data(), 1, utf.size(), out_);
}

void VramMirror::emitCellChange(int row, int col, uint8_t newCode)
{
    /* Position the host cursor at (row, col) if we're not already
     * there.  ANSI is 1-based on both axes. */
    if (hostCursorRow_ != row || hostCursorCol_ != col) {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "\x1B[%d;%dH",
                              row + 1, col + 1);
        if (n > 0) emitAnsi(std::string_view(buf, static_cast<size_t>(n)));
        hostCursorRow_ = row;
        hostCursorCol_ = col;
    }
    emitKoi8(newCode);
    /* After emit, host's terminal advances by one column. */
    hostCursorCol_++;
    cursorRow_ = row;
    cursorCol_ = col;
}

void VramMirror::flushFrame()
{
    if (emu_ == nullptr) return;
    rebuildFontIfNeeded();

    /* Idle counter: bumped each flush.  Reset only by SUBSTANTIAL write
     * activity since the previous flush — small bursts (≤ 32 bytes,
     * ie up to four cell-glyphs of 8 bytes each) are taken to be cursor
     * blink and don't disturb the idle signal, so a kernel parked at a
     * prompt with a blinking cursor still registers as quiescent and
     * the CLI's input gate opens.  Real OS-driven output (banner,
     * command echo, directory listing) writes far more than 32 bytes
     * per emu frame and resets the counter cleanly. */
    constexpr int kBlinkByteThreshold = 32;
    if (writesThisFlush_ > kBlinkByteThreshold) {
        framesIdle_ = 0;
    } else {
        ++framesIdle_;
    }
    writesThisFlush_ = 0;

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const int idx = r * kCols + c;
            if (!dirty_[idx]) continue;
            const uint64_t key = readGlyphKey(r, c);
            const uint8_t  code = lookup(key);
            /* Skip unknown glyphs and leave the cell dirty — the most
             * common cause is "caught mid-paint": the OS writes the
             * eight scanline bytes of a glyph one at a time, partial
             * states decode as kUnknownGlyph.  Re-flushing next frame
             * picks up the settled glyph.  A genuinely-custom glyph
             * (RAM-loaded font, OS-drawn icon) just stays invisible
             * for now — better than a screenful of █ markers while
             * boot ROM paints character by character.
             *
             * TODO: shadow-RAM font for OSes like Rodionov that load
             * a custom font into RAM at boot. */
            if (code == kUnknownGlyph) continue;
            dirty_[idx] = false;
            if (shadow_[idx] == code) continue;
            shadow_[idx] = code;
            emitCellChange(r, c, code);
        }
    }

    /* Park the host-terminal cursor at the last cell the OS wrote to.
     * The OS's own cursor-glyph (block / underline / blink) often
     * doesn't decode to a font code we emit, so the host cursor is
     * our only visible cursor.  Re-positioning every flush makes the
     * cursor follow what the guest is doing — including the case
     * where the user types a space (no character delta emitted, but
     * the OS still advanced its cursor and we mirror that). */
    if (lastWriteRow_ >= 0 &&
        (hostCursorRow_ != lastWriteRow_ || hostCursorCol_ != lastWriteCol_))
    {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "\x1B[%d;%dH",
                              lastWriteRow_ + 1, lastWriteCol_ + 1);
        if (n > 0) emitAnsi(std::string_view(buf, static_cast<size_t>(n)));
        hostCursorRow_ = lastWriteRow_;
        hostCursorCol_ = lastWriteCol_;
    }

    if (out_) std::fflush(out_);
}

std::string VramMirror::Snapshot::row(int r) const
{
    if (r < 0 || r >= kRows) return {};
    std::string s;
    s.reserve(static_cast<size_t>(kCols));
    for (int c = 0; c < kCols; ++c)
        s.push_back(static_cast<char>(cells[r * kCols + c]));
    while (!s.empty() && static_cast<uint8_t>(s.back()) == 0x20)
        s.pop_back();
    return s;
}

VramMirror::Snapshot VramMirror::snapshot() const
{
    Snapshot s;
    s.cells = shadow_;
    return s;
}

} /* namespace ms0515 */
