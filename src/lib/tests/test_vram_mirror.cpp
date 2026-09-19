/*
 * test_vram_mirror.cpp — ms0515::VramMirror end-to-end.
 *
 * Drives the real Emulator: load a ROM, attach the mirror, poke
 * VRAM through the public memory API, call flushFrame(), and check
 * the history string + cursor position.  Going through the real
 * font map (built from the shipped ms0515-romb.rom) is intentional —
 * the unit-test asserts the mirror's behavior under the exact ROM
 * the CLI / frontend use.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>

#include "../src/EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/memory.h>
}

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kCols = ms0515::VramMirror::kCols;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

const std::string kRomPath =
    std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom";

/* Map VRAM at virtual 0..037777 so writeByte(0..) lands directly in
 * VRAM offset 0.. — bit 7 of the dispatcher enables VRAM, window bits
 * 10/11 both zero put it at the low window. */
void enable_vram_window(ms0515::Emulator &emu)
{
    auto &mem = ms0515::internal::board(emu).mem;
    mem.dispatcher = MEM_DISP_VRAM_EN;
}

/* Paint a single glyph (8-byte bitmap) at cell (row, col) by writing
 * the eight scanline bytes through the public memory API. */
void paint_glyph(ms0515::Emulator &emu,
                 int row, int col,
                 const uint8_t glyph[8])
{
    for (int y = 0; y < 8; ++y) {
        const uint16_t addr = static_cast<uint16_t>((row * 8 + y) * 80 + col);
        emu.writeByte(addr, glyph[y]);
    }
}

/* Look up the 8-byte bitmap of a printable ASCII char from the ROM
 * font.  Uses the same anchor-by-shape trick the mirror itself uses
 * so tests don't hardcode a ROM-revision-specific font base. */
struct RomFont {
    int base = -1;
    bool resolved = false;
    void resolve(const ms0515::Emulator &emu) {
        if (resolved) return;
        auto rom = ms0515::internal::rom(emu);
        static constexpr uint8_t kAnchorZero[8] = {
            0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
        };
        constexpr int kAnchorIdx = '0' - 0x20;
        for (size_t off = 0; off + 8 <= rom.size(); ++off) {
            if (std::memcmp(rom.data() + off, kAnchorZero, 8) == 0) {
                base = static_cast<int>(off) - kAnchorIdx * 8;
                break;
            }
        }
        resolved = true;
    }
    const uint8_t *glyphFor(const ms0515::Emulator &emu, char c) {
        resolve(emu);
        REQUIRE(base >= 0);
        auto rom = ms0515::internal::rom(emu);
        int off = base + (static_cast<int>(c) - 0x20) * 8;
        REQUIRE(off >= 0);
        REQUIRE(off + 8 <= static_cast<int>(rom.size()));
        return rom.data() + off;
    }
};

}  // namespace

TEST_SUITE("VramMirror") {

TEST_CASE("attach / flush of a single ROM glyph emits the matching ASCII char") {
    if (!std::filesystem::exists(kRomPath)) {
        WARN("ms0515-romb.rom missing — skipping");
        return;
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);

    /* Boot reset just paints all-zero into VRAM (the dispatcher hasn't
     * been touched yet) — flush the initial "all blanks" state first so
     * subsequent paint+flush only shows the actual delta. */
    mirror.flushFrame();
    mirror.clearHistory();

    RomFont font;
    paint_glyph(emu, /*row=*/0, /*col=*/5, font.glyphFor(emu, 'A'));
    mirror.flushFrame();

    CHECK(mirror.cursorRow() == 0);
    CHECK(mirror.cursorCol() == 5);
    CHECK(mirror.history() == "A");
    auto snap = mirror.snapshot();
    CHECK(snap.cells[0 * kCols + 5] == 'A');
}

TEST_CASE("multiple glyphs in row produce in-order history") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();
    mirror.clearHistory();

    RomFont font;
    paint_glyph(emu, 1, 0, font.glyphFor(emu, 'D'));
    paint_glyph(emu, 1, 1, font.glyphFor(emu, 'I'));
    paint_glyph(emu, 1, 2, font.glyphFor(emu, 'R'));
    mirror.flushFrame();

    CHECK(mirror.history() == "DIR");
    CHECK(mirror.cursorRow() == 1);
    CHECK(mirror.cursorCol() == 2);
}

TEST_CASE("idempotent flush — re-flushing without VRAM changes emits nothing") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();
    mirror.clearHistory();

    RomFont font;
    paint_glyph(emu, 2, 3, font.glyphFor(emu, 'X'));
    mirror.flushFrame();
    CHECK(mirror.history() == "X");

    mirror.flushFrame();      /* nothing new in VRAM */
    CHECK(mirror.history() == "X");
}

TEST_CASE("overwriting a cell with a different glyph re-emits the new char") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();
    mirror.clearHistory();

    RomFont font;
    paint_glyph(emu, 0, 0, font.glyphFor(emu, 'A'));
    mirror.flushFrame();
    paint_glyph(emu, 0, 0, font.glyphFor(emu, 'B'));
    mirror.flushFrame();

    CHECK(mirror.history() == "AB");
    auto snap = mirror.snapshot();
    CHECK(snap.cells[0 * kCols + 0] == 'B');
}

TEST_CASE("FILE* output writes ANSI cursor positioning + UTF-8 char") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    /* Open a temp file path explicitly so we don't fight MSVC's
     * deprecation of std::tmpfile() (CRT preferred sibling is the
     * not-quite-portable `tmpfile_s`).  Removed at the end of the
     * test. */
    const auto tmpPath =
        (std::filesystem::temp_directory_path() / "ms0515_vrammirror_test.bin")
            .string();
    FILE *tmp = nullptr;
#if defined(_MSC_VER)
    REQUIRE(fopen_s(&tmp, tmpPath.c_str(), "wb+") == 0);
#else
    tmp = std::fopen(tmpPath.c_str(), "wb+");
#endif
    REQUIRE(tmp != nullptr);

    ms0515::VramMirror mirror;
    mirror.setOutput(tmp);
    mirror.attach(emu);
    mirror.flushFrame();
    /* Drain whatever the all-blanks first flush wrote. */
    std::fflush(tmp);
    long drained = std::ftell(tmp);

    RomFont font;
    paint_glyph(emu, 4, 7, font.glyphFor(emu, 'Z'));
    mirror.flushFrame();
    std::fflush(tmp);
    std::fseek(tmp, drained, SEEK_SET);

    std::string out;
    int ch;
    while ((ch = std::fgetc(tmp)) != EOF) out.push_back(static_cast<char>(ch));
    std::fclose(tmp);
    std::filesystem::remove(tmpPath);

    /* Expect ESC[?2026h ESC[5;8H Z ESC[?2026l — the cell paint
     * sandwiched between the DEC 2026 synchronized-output begin/end
     * pair.  No '_' write happened so the OS cursor detector never
     * fires; no cursor positioning escape, no '?25h' / '?25l'. */
    CHECK(out == "\x1B[?2026h\x1B[5;8HZ\x1B[?2026l");
}

TEST_CASE("invalidate() forces a re-decode on next flush, and the whole screen is emitted again") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();

    /* Bypass the public API: poke VRAM directly so the hook doesn't fire.
     * This simulates the post-loadState case where many bytes change in
     * one go without per-byte notifications. */
    RomFont font;
    auto &mem = ms0515::internal::board(emu).mem;
    const uint8_t *glyph = font.glyphFor(emu, 'Q');
    for (int y = 0; y < 8; ++y) {
        mem.vram[(0 * 8 + y) * 80 + 9] = glyph[y];
    }
    mirror.flushFrame();
    /* Without invalidate, the dirty bitmap is empty → no emission. */
    auto historyBefore = mirror.history();

    mirror.invalidate();
    mirror.flushFrame();
    /* After invalidate, every cell is emitted again - the host's screen may
     * hold something else by now - the changed cell among them. */
    const std::string emitted = mirror.history().substr(historyBefore.size());
    CHECK(emitted.find('Q') != std::string::npos);
    CHECK(emitted.size() > 80);
}

TEST_CASE("detach stops further writes from registering") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    enable_vram_window(emu);

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();
    mirror.clearHistory();

    mirror.detach();

    RomFont font;
    paint_glyph(emu, 0, 0, font.glyphFor(emu, 'A'));
    mirror.flushFrame();
    CHECK(mirror.history().empty());
}

namespace {

/* The glyphs of one row, painted from 8-byte bitmaps, as the mirror reads
 * them back. */
std::string paintedRow(ms0515::Emulator &emu, const std::vector<std::array<uint8_t, 8>> &glyphs)
{
    enable_vram_window(emu);
    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.flushFrame();
    mirror.clearHistory();
    for (size_t i = 0; i < glyphs.size(); ++i)
        paint_glyph(emu, 0, static_cast<int>(i), glyphs[i].data());
    mirror.flushFrame();
    std::string out;
    const auto snap = mirror.snapshot();
    for (size_t i = 0; i < glyphs.size(); ++i)
        out += ms0515::VramMirror::utf8FromKoi8(snap.cells[i]);
    return out;
}

/* A glyph key as the mirror packs it (scanline 0 in the low byte). */
std::array<uint8_t, 8> glyphOf(uint64_t key)
{
    std::array<uint8_t, 8> g{};
    for (int y = 0; y < 8; ++y) g[y] = static_cast<uint8_t>(key >> (8 * y));
    return g;
}

}  // namespace

TEST_CASE("ROM-B's pseudographics (its table at 157000, codes 200-277) read as the lines they draw") {
    if (!std::filesystem::exists(kRomPath)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomPath));
    auto rom = ms0515::internal::rom(emu);
    /* ROM-B's table, found by its corner at code 240 (as the mirror does) */
    static constexpr uint8_t kCorner[8] = {0, 0, 0, 0, 0x1F, 0x18, 0x18, 0x18};
    int base = -1;
    for (size_t off = 0; off + 8 <= rom.size() && base < 0; ++off)
        if (std::memcmp(rom.data() + off, kCorner, 8) == 0) base = static_cast<int>(off) - 040 * 8;
    REQUIRE(base >= 0);
    auto at = [&](int code) {
        std::array<uint8_t, 8> g{};
        std::memcpy(g.data(), rom.data() + base + (code - 0200) * 8, 8);
        return g;
    };
    /* the double and the mixed lines are ROM-B's own drawing */
    CHECK(paintedRow(emu, {at(0220), at(0224), at(0226), at(0221)}) == "╔═╦╗");
    CHECK(paintedRow(emu, {at(0225), at(0231), at(0232), at(0227)}) == "║╠╬╣");
    CHECK(paintedRow(emu, {at(0202), at(0200), at(0216), at(0204)}) == "╤╧╟╢");
    CHECK(paintedRow(emu, {at(0240), at(0244), at(0252), at(0245)}) == "┌─┼│");
    CHECK(paintedRow(emu, {at(0262), at(0263), at(0264), at(0265)}) == "╭╮╯╰");
    CHECK(paintedRow(emu, {at(0266), at(0267), at(0273), at(0274), at(0275)}) == "→←±№¤");
}

TEST_CASE("Rodionov's glyphs read as his signs; the shades by their density; ╘ is no ©") {
    const std::string romA = std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom";
    if (!std::filesystem::exists(romA)) return;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(romA));
    CHECK(paintedRow(emu, {glyphOf(0x3C4299A1A199423CULL), glyphOf(0x00000000003C663CULL),
                           glyphOf(0x0080E0F8FEF8E080ULL), glyphOf(0x00020E3EFE3E0E02ULL),
                           glyphOf(0x080C0EFF0E0C0800ULL), glyphOf(0x103070FF70301000ULL),
                           glyphOf(0x007E0018187E1818ULL), glyphOf(0x00888898B8E8CB8BULL),
                           glyphOf(0x0018007E00180000ULL)}) == "©°►◄→←±№÷");
    CHECK(paintedRow(emu, {glyphOf(0x8822882288228822ULL), glyphOf(0xAA55AA55AA55AA55ULL),
                           glyphOf(0xEEDB77DBEEDB77DBULL)}) == "░▒▓");
    CHECK(paintedRow(emu, {glyphOf(0x0000001F181F1818ULL), glyphOf(0x1818181F00000000ULL)}) == "╘┌");
    CHECK(paintedRow(emu, {glyphOf(0x183C7EFF18181818ULL), glyphOf(0x18181818FF7E3C18ULL)}) == "↓↑");
}

}  // TEST_SUITE
