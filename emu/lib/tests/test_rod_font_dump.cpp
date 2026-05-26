/*
 * test_rod_font_dump.cpp — Diagnostic dump of Rodionov RT-15SJ RAM / VRAM.
 *
 * Marked with doctest::skip so the regular suite ignores it.  Run with
 *   ms0515_lib_tests.exe --test-case="DIAG: Rodionov font dump" --no-skip
 * to produce two files under the test build dir:
 *   - rod_ram.bin   (128 KB raw main RAM)
 *   - rod_vram.bin  (16 KB raw video RAM)
 * Plus a CSV (rod_unknown_cells.csv) listing every cell whose 8-byte
 * bitmap the ROM-built glyph map can't resolve — those are exactly the
 * cells that ship to the user as █ today and are the targets we want to
 * teach VramMirror about.
 *
 * Delete this file once the Rodionov font extraction is folded into
 * the production glyph-build path.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>

#include "EmulatorInternal.hpp"
#include "test_disk.hpp"

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/memory.h>
}

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeBlob(const fs::path &dst, const void *data, std::size_t bytes)
{
    std::ofstream out(dst, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data),
              static_cast<std::streamsize>(bytes));
}

void tap(ms0515::Emulator &emu, ms0515::Key key, int settleFrames = 8)
{
    emu.keyPress(key, true);
    (void)emu.stepFrame();
    emu.keyPress(key, false);
    for (int i = 0; i < settleFrames; ++i) (void)emu.stepFrame();
}

void typeChar(ms0515::Emulator &emu, char c)
{
    using K = ms0515::Key;
    switch (c) {
        case '0': tap(emu, K::Digit0); return;
        case '1': tap(emu, K::Digit1); return;
        case '2': tap(emu, K::Digit2); return;
        case '3': tap(emu, K::Digit3); return;
        case '4': tap(emu, K::Digit4); return;
        case '5': tap(emu, K::Digit5); return;
        case '6': tap(emu, K::Digit6); return;
        case '7': tap(emu, K::Digit7); return;
        case '8': tap(emu, K::Digit8); return;
        case '9': tap(emu, K::Digit9); return;
        case '-': tap(emu, K::MinusEq); return;
        case '\r': tap(emu, K::Return, 60); return;
        default:  FAIL("typeChar: unsupported '" << c << "'");
    }
}

}  /* namespace */

TEST_CASE("DIAG: Rodionov font dump" * doctest::skip())
{
    const fs::path outDir = fs::path(TESTS_BUILD_DIR) / "rod-font-dump";
    fs::create_directories(outDir);

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));

    ms0515_test::TempDisk td{std::string{ASSETS_DIR} + "/disks/rodionov.dsk"};
    REQUIRE(emu.mountDisk(0, td.path().string()));
    std::error_code ec;
    if (auto sz = fs::file_size(td.path(), ec); !ec && sz == 2 * 409600u)
        (void)emu.mountDisk(2, td.path().string());
    emu.enableRamDisk();
    emu.reset();

    /* Boot to the date prompt (Rodionov asks "ВВЕД [ДД-MM-ГГ]?"
     * before handing control to ROSA Commander).  ~2500 frames is
     * enough to get there; the existing DIAG test uses the same
     * window. */
    for (int i = 0; i < 2500; ++i)
        (void)emu.stepFrame();

    /* Try several plausible date formats — Rodionov is picky and
     * just re-prompts on bad input.  After one accepts we land in
     * ROSA Commander where the pseudographic borders appear. */
    for (char c : std::string{"01-06-93\r"}) typeChar(emu, c);
    for (int i = 0; i < 400; ++i) (void)emu.stepFrame();
    for (char c : std::string{"\r"})         typeChar(emu, c);   /* time? */
    for (int i = 0; i < 1500; ++i) (void)emu.stepFrame();

    const auto &board = ms0515::internal::board(emu);
    const uint8_t *vram = board_get_vram(&board);

    writeBlob(outDir / "rod_ram.bin",  board.mem.ram,  MEM_RAM_SIZE);
    writeBlob(outDir / "rod_vram.bin", vram,           MEM_VRAM_SIZE);

    /* Pull cells whose 8-byte bitmap doesn't resolve via the
     * VramMirror glyph map — those are exactly the pseudographic
     * shapes Rodionov loads into RAM, which we want to teach the
     * mirror about. */
    const auto outFile =
        (fs::path(outDir) / "emit.bin").string();
    FILE *emitFp = nullptr;
#if defined(_MSC_VER)
    REQUIRE(fopen_s(&emitFp, outFile.c_str(), "wb") == 0);
#else
    emitFp = std::fopen(outFile.c_str(), "wb");
#endif
    REQUIRE(emitFp);

    ms0515::VramMirror mirror;
    mirror.setOutput(emitFp);
    mirror.attach(emu);
    mirror.invalidate();      /* force decode of all cells */
    mirror.flushFrame();
    std::fclose(emitFp);
    auto snap = mirror.snapshot();

    constexpr int kCols = ms0515::VramMirror::kCols;
    constexpr int kRows = ms0515::VramMirror::kRows;

    auto csv = std::ofstream(outDir / "rod_unknown_cells.csv");
    csv << "row,col,b0,b1,b2,b3,b4,b5,b6,b7\n";
    int unknownCount = 0;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const uint8_t code =
                snap.cells[static_cast<std::size_t>(r * kCols + c)];
            if (code != ms0515::VramMirror::kUnknownGlyph) continue;
            ++unknownCount;
            csv << r << ',' << c;
            for (int y = 0; y < 8; ++y) {
                const int off = (r * 8 + y) * 80 + c;
                csv << ',' << static_cast<int>(vram[off]);
            }
            csv << '\n';
        }
    }
    csv.close();

    std::fprintf(stderr,
        "[diag] Rodionov dump: %d unknown cells.  Files in %s\n",
        unknownCount, outDir.string().c_str());

    /* Print every non-blank screen row — handy to confirm Rodionov
     * actually advanced past the BIOS to a screen containing
     * pseudographics. */
    for (int r = 0; r < kRows; ++r) {
        const auto rt = snap.row(r);
        if (rt.empty()) continue;
        std::fprintf(stderr, "[diag] row %2d: %.*s\n", r,
                     static_cast<int>(rt.size()), rt.data());
    }

    /* Row 5 holds the highlighted file (SWAP.SYS by default).  As a
     * cheap sanity check that the XOR-fallback path is wiring through,
     * verify at least one cell of row 5 is marked inverted. */
    bool anyInverted = false;
    for (int c = 0; c < kCols && !anyInverted; ++c)
        anyInverted = snap.inverted[static_cast<std::size_t>(5 * kCols + c)];

    CHECK(unknownCount == 0);
    CHECK(anyInverted);
}
