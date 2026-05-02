/*
 * test_screen_reader.cpp — ScreenReader cache behaviour.
 *
 * The reader keeps a per-cell cache of the previous frame's glyph
 * keys and decoded codes so unchanged cells skip the font-table
 * lookup.  These tests pin that cache down so future changes don't
 * silently regress correctness — feeding identical, mutated, and
 * mode-switched VRAM and checking that the returned Snapshot still
 * matches a from-scratch decode.
 */

#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include <ms0515/ScreenReader.hpp>
#include <ms0515/memory.h>

#include "test_disk.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

/* Load an MS0515 ROM image so buildFont() has something real to work
 * with.  Any of the shipped ROMs will do — both register the same
 * KOI-8 main font, which is the only thing the decoder cares about. */
[[nodiscard]] std::vector<uint8_t> loadRom()
{
    const auto path = fs::path{ASSETS_DIR} / "rom" / "ms0515-roma.rom";
    std::ifstream f{path, std::ios::binary};
    REQUIRE(f.is_open());
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>{});
    REQUIRE(rom.size() == MEM_ROM_SIZE);
    return rom;
}

/* Render the byte string `text` into a hires VRAM buffer at row 0
 * starting at column 0, using the same font layout the ROM and
 * ScreenReader agree on.  We don't actually draw glyphs; we forge
 * the 8-byte cell bitmaps to match the ROM's main-font entries by
 * reusing buildFont's anchor logic via ScreenReader.  Easier: copy
 * the glyph bytes straight from the ROM's main-font table. */
static void renderRow(std::span<uint8_t> vram,
                      std::span<const uint8_t> rom,
                      int kMainFontFileOff, int row,
                      std::string_view text)
{
    constexpr int kBytesPerLine = 80;
    for (size_t c = 0; c < text.size(); ++c) {
        uint8_t code = static_cast<uint8_t>(text[c]);
        int glyphOff = kMainFontFileOff + (code - 0x20) * 8;
        for (int y = 0; y < 8; ++y) {
            int dst = (row * 8 + y) * kBytesPerLine + static_cast<int>(c);
            vram[static_cast<size_t>(dst)] = rom[static_cast<size_t>(glyphOff + y)];
        }
    }
}

/* Re-derive the main-font file offset the same way ScreenReader does:
 * find the visual shape of '0' (KOI-8 0x30) and back off by 16 entries
 * (0x30 - 0x20 = 16 glyphs from the start of the main font). */
static int findMainFontOffset(std::span<const uint8_t> rom)
{
    static constexpr uint8_t kAnchorZero[8] = {
        0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
    };
    for (size_t off = 0; off + 8 <= rom.size(); ++off) {
        if (std::memcmp(rom.data() + off, kAnchorZero, 8) == 0)
            return static_cast<int>(off) - (0x30 - 0x20) * 8;
    }
    return -1;
}

} /* namespace */

/* Find ALL occurrences of an 8-byte glyph anchor in ROM, returning
 * the implied font base for each.  Used by the diagnostic below to
 * count how many distinct font tables ROM-A actually contains. */
static std::vector<int> findAllFontBases(std::span<const uint8_t> rom,
                                         const uint8_t (&anchor)[8],
                                         int anchorIndex)
{
    std::vector<int> bases;
    if (rom.size() < 8) return bases;
    const auto end = rom.size() - 8;
    for (size_t off = 0; off <= end; ++off) {
        if (std::memcmp(rom.data() + off, anchor, 8) == 0) {
            const int base = static_cast<int>(off) - anchorIndex * 8;
            if (base >= 0) bases.push_back(base);
        }
    }
    return bases;
}

/* Count the slots in a 64-glyph alt-font table that contain
 * non-zero pixel data — a high count means it really is a font
 * table; a low count probably indicates a coincidental bitmap
 * match that happens to share the anchor's pattern. */
static int densityOfAltFontAt(std::span<const uint8_t> rom, int base)
{
    int nonzero = 0;
    for (int i = 0; i < 64; ++i) {
        const int off = base + i * 8;
        if (off + 8 > static_cast<int>(rom.size())) break;
        for (int b = 0; b < 8; ++b) {
            if (rom[static_cast<size_t>(off + b)] != 0) {
                ++nonzero;
                break;
            }
        }
    }
    return nonzero;
}

TEST_SUITE("ScreenReader") {

TEST_CASE("DIAG: appendKoi8Char emits valid UTF-8 bytes for Cyrillic") {
    /* Direct byte-level check: KOI-8R 0xE1 is Cyrillic capital 'А'
     * (Unicode U+0410), which in UTF-8 is exactly two bytes D0 90.
     * If we see anything else in the buffer, the compiler is
     * reading our source-file string literals in the wrong
     * encoding, and that's why the user sees U+FFFD replacement
     * characters in the Terminal window. */
    std::string s;
    ms0515::ScreenReader::appendKoi8Char(s, 0xE1);
    std::fprintf(stderr, "[diag] bytes for KOI-8 0xE1 ('А'):");
    for (unsigned char c : s)
        std::fprintf(stderr, " %02x", c);
    std::fprintf(stderr, "  (length=%zu, expect d0 90 length=2)\n", s.size());

    /* Also dump 'ю' (KOI-8 0xC0 → UTF-8 D1 8E) and 'я' (0xD1 → D1 8F). */
    s.clear();
    ms0515::ScreenReader::appendKoi8Char(s, 0xC0);
    std::fprintf(stderr, "[diag] bytes for KOI-8 0xC0 ('ю'):");
    for (unsigned char c : s)
        std::fprintf(stderr, " %02x", c);
    std::fprintf(stderr, "  (expect d1 8e length=2)\n");

    s.clear();
    ms0515::ScreenReader::appendKoi8Char(s, 0xD1);
    std::fprintf(stderr, "[diag] bytes for KOI-8 0xD1 ('я'):");
    for (unsigned char c : s)
        std::fprintf(stderr, " %02x", c);
    std::fprintf(stderr, "  (expect d1 8f length=2)\n");

    CHECK(true);
}

TEST_CASE("DIAG: enumerate font tables in ROM-A" * doctest::skip()) {
    auto rom = loadRom();

    static constexpr uint8_t kAnchorZero[8] = {
        0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00,
    };
    static constexpr uint8_t kAnchorCyrA[8] = {
        0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00,
    };

    auto mainBases = findAllFontBases(rom, kAnchorZero, 0x30 - 0x20);
    auto altBases  = findAllFontBases(rom, kAnchorCyrA, 33);

    std::fprintf(stderr,
        "[diag] kAnchorZero ('0' shape): %zu match(es)\n",
        mainBases.size());
    for (int base : mainBases) {
        int density = 0;
        for (int i = 0; i < 96; ++i) {
            const int off = base + i * 8;
            if (off + 8 > static_cast<int>(rom.size())) break;
            for (int b = 0; b < 8; ++b)
                if (rom[static_cast<size_t>(off + b)] != 0) {
                    ++density; break;
                }
        }
        std::fprintf(stderr, "[diag]   base=0x%04x density=%d/96\n",
                     base, density);
    }
    std::fprintf(stderr,
        "[diag] kAnchorCyrA ('А' shape): %zu match(es)\n",
        altBases.size());
    for (int base : altBases) {
        std::fprintf(stderr, "[diag]   base=0x%04x density=%d/64\n",
                     base, densityOfAltFontAt(rom, base));
    }
    /* The test is informational — it always passes; we just want
     * the report on stderr to drive font-recognition work. */
    CHECK(true);
}

TEST_CASE("DIAG: dump unrecognised cells from Mihin POST screen" * doctest::skip()) {
    auto rom = loadRom();
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} +
                            "/rom/ms0515-roma.rom"));

    /* Mount Mihin so the OS gets past POST eventually. */
    ms0515_test::TempDisk td{std::string{TESTS_DIR} +
                              "/disks/test_mihin.dsk"};
    (void)emu.mountDisk(0, td.path().string());
    emu.reset();

    ms0515::ScreenReader sr;
    sr.buildFont(rom);
    const int fontOff = findMainFontOffset(rom);

    /* Sample at several points during boot — POST appears in early
     * frames, then disappears as the OS overwrites it with the
     * loader banner.  We dump each interesting moment. */
    auto dumpAt = [&](const char *label, int maxBitmaps){
        std::fprintf(stderr, "[diag] === %s ===\n", label);
        const bool hires = emu.isHires();
        std::fprintf(stderr, "[diag] mode = %s\n",
                     hires ? "HIRES" : "LORES");
        const int colStride = hires ? 1 : 2;
        auto cur = sr.readScreen({emu.vram(), MEM_VRAM_SIZE}, hires);
        const auto vram = std::span<const uint8_t>(emu.vram(), MEM_VRAM_SIZE);

        /* Show row text first. */
        for (int r = 0; r < ms0515::ScreenReader::kRows; ++r) {
            const auto rt = cur.row(r);
            if (!rt.empty())
                std::fprintf(stderr, "[diag]   %2d: %.*s\n", r,
                             (int)rt.size(), rt.data());
        }
        /* Then the first few unrecognised bitmaps. */
        int dumped = 0;
        for (int r = 0; r < ms0515::ScreenReader::kRows && dumped < maxBitmaps; ++r) {
            for (int c = 0; c < cur.cols && dumped < maxBitmaps; ++c) {
                if (cur.cells[r * ms0515::ScreenReader::kHiresCols + c]
                    != ms0515::ScreenReader::kUnknownGlyph)
                    continue;
                std::fprintf(stderr, "[diag]   r=%d c=%d:", r, c);
                for (int y = 0; y < 8; ++y) {
                    const int off = (r * 8 + y) * 80 + c * colStride;
                    std::fprintf(stderr, " %02x",
                                 vram[static_cast<size_t>(off)]);
                }
                std::fputc('\n', stderr);
                ++dumped;
            }
        }
    };

    /* Take checkpoints during the boot sequence. */
    for (int i = 0; i < 30;  ++i) (void)emu.stepFrame();
    dumpAt("frame 30",  3);
    for (int i = 0; i < 60;  ++i) (void)emu.stepFrame();
    dumpAt("frame 90",  3);
    for (int i = 0; i < 100; ++i) (void)emu.stepFrame();
    dumpAt("frame 190 (POST visible)", 6);
    for (int i = 0; i < 60;  ++i) (void)emu.stepFrame();
    dumpAt("frame 250 (POST late)", 6);

    (void)fontOff;
    CHECK(true);
}


TEST_CASE("readScreen with identical VRAM returns identical Snapshot") {
    auto rom = loadRom();
    ms0515::ScreenReader sr;
    sr.buildFont(rom);

    const int fontOff = findMainFontOffset(rom);
    REQUIRE(fontOff >= 0);

    std::array<uint8_t, MEM_VRAM_SIZE> vram{};
    renderRow(vram, rom, fontOff, /*row=*/0, "HELLO");

    auto first  = sr.readScreen(vram, /*hires=*/true);
    auto second = sr.readScreen(vram, /*hires=*/true);

    CHECK(first.cols == second.cols);
    CHECK(first.cells == second.cells);
    CHECK(first.row(0) == "HELLO");
    CHECK(second.row(0) == "HELLO");
}

TEST_CASE("readScreen reflects per-cell VRAM changes after caching") {
    auto rom = loadRom();
    ms0515::ScreenReader sr;
    sr.buildFont(rom);
    const int fontOff = findMainFontOffset(rom);
    REQUIRE(fontOff >= 0);

    std::array<uint8_t, MEM_VRAM_SIZE> vram{};
    renderRow(vram, rom, fontOff, 0, "ABC");
    auto snap1 = sr.readScreen(vram, true);
    REQUIRE(snap1.row(0) == "ABC");

    /* Swap the second column from 'B' to 'X' by overwriting the
     * eight glyph bytes belonging to that cell. */
    renderRow(vram, rom, fontOff, 0, "AXC");
    auto snap2 = sr.readScreen(vram, true);
    CHECK(snap2.row(0) == "AXC");

    /* Cells we didn't touch stay at the cached value. */
    CHECK(snap2.cells[0] == 'A');
    CHECK(snap2.cells[2] == 'C');
}

TEST_CASE("Mode switch invalidates the cache") {
    auto rom = loadRom();
    ms0515::ScreenReader sr;
    sr.buildFont(rom);
    const int fontOff = findMainFontOffset(rom);
    REQUIRE(fontOff >= 0);

    std::array<uint8_t, MEM_VRAM_SIZE> vram{};
    renderRow(vram, rom, fontOff, 0, "HI");

    /* Render in hires (cell at col 0 reads from byte at offset
     * row*8*80 + 0) — establishes a cached key. */
    auto hires = sr.readScreen(vram, /*hires=*/true);
    CHECK(hires.cols == ms0515::ScreenReader::kHiresCols);
    CHECK(hires.row(0) == "HI");

    /* Now claim it's lores.  Cell-to-VRAM mapping changes
     * (col 0 now reads from byte 0, col 1 from byte 2, …) so a
     * stale cache would mis-decode.  After invalidation the
     * snapshot reflects the new mapping. */
    auto lores = sr.readScreen(vram, /*hires=*/false);
    CHECK(lores.cols == ms0515::ScreenReader::kLoresCols);
    /* Cell 0 in lores reads byte 0 of each scan line — same byte
     * as cell 0 in hires — so it still resolves to 'H'. */
    CHECK(lores.cells[0] == 'H');

    /* Flip back and confirm hires still sees "HI". */
    auto hires2 = sr.readScreen(vram, /*hires=*/true);
    CHECK(hires2.row(0) == "HI");
}

TEST_CASE("buildFont() invalidates the cache") {
    auto rom = loadRom();
    ms0515::ScreenReader sr;
    sr.buildFont(rom);
    const int fontOff = findMainFontOffset(rom);
    REQUIRE(fontOff >= 0);

    std::array<uint8_t, MEM_VRAM_SIZE> vram{};
    renderRow(vram, rom, fontOff, 0, "Z");
    auto first = sr.readScreen(vram, true);
    REQUIRE(first.cells[0] == 'Z');

    /* Rebuild the font from an empty ROM — every glyph slot reads as
     * the all-zero key, which the reader treats as blank.  The decode
     * must reflect this; without invalidation the cached 'Z' would
     * leak through. */
    std::vector<uint8_t> emptyRom(MEM_ROM_SIZE, 0);
    sr.buildFont(emptyRom);
    auto after = sr.readScreen(vram, true);
    /* The 'Z' glyph bytes are still in VRAM but the new font doesn't
     * map them to anything, so the cell must no longer decode to
     * 'Z'.  The exact replacement (0x20 blank or kUnknownGlyph)
     * depends on the lookup() heuristics — what matters here is
     * that buildFont() actually invalidated the per-cell cache and
     * forced re-decoding against the new (empty) font. */
    CHECK(after.cells[0] != 'Z');
}

} /* TEST_SUITE */
