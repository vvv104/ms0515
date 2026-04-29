/*
 * test_boot.cpp — Boot smoke tests.
 *
 * Loads real ROM + disk combinations and verifies the system boots
 * successfully: no HALT, no tight loop, VRAM populated, peripherals
 * initialised.
 *
 * ROMs are discovered from ASSETS_DIR/rom (.rom files); disks from
 * TESTS_DIR/disks (.dsk files).  Adding a new ROM or test-fixture
 * disk to the corresponding directory automatically creates new
 * test cases.  emu/assets/disks/ is reserved for the original-OS
 * images shipped to end users and is intentionally not exercised
 * by the suite.
 *
 * These are integration tests that exercise the entire stack:
 * CPU → memory → board → timer → keyboard → FDC.
 */

#include <doctest/doctest.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Disassembler.hpp>
#include <ms0515/Emulator.hpp>
#include <ms0515/ScreenReader.hpp>

#include "test_disk.hpp"

namespace fs = std::filesystem;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

TEST_SUITE("Boot") {

/* ── Asset discovery ────────────────────────────────────────────────────── */

/* ROMs come from the release-side assets tree (where they ride along
 * to end users); disk fixtures come from the tests tree.  The boot
 * suite never touches emu/assets/disks/ — that directory is reserved
 * for the original-OS images that ship in the package, and we
 * exercise the emulator against the trimmed-OS test_*.dsk fixtures
 * instead. */
static const std::string kRomDir    = std::string{ASSETS_DIR} + "/rom";
static const std::string kDiskDir   = std::string{TESTS_DIR}  + "/disks";

/* Collect all files matching an extension in a directory.
 * Returns filenames sorted alphabetically for stable ordering. */
static std::vector<std::string> discoverFiles(const std::string &dir,
                                              const std::string &ext)
{
    std::vector<std::string> result;
    if (!fs::is_directory(dir))
        return result;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ext)
            continue;
        result.push_back(entry.path().filename().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

/* ── Known-bad combinations ─────────────────────────────────────────────── */

/* ROM+disk pairs that are known to fail.  These are tested with WARN
 * instead of CHECK so the suite stays green while the issues are
 * documented.  See also: docs/kb/KNOWN_ISSUES.md */
static const std::set<std::pair<std::string, std::string>> kKnownBad = {
    /* test_omega.dsk inherits Omega's video-mode setup from omega-lang
     * — boots fine on the patched ROM-A but turns the screen pink and
     * stalls on the unpatched ms0515-roma-original.rom.  See the
     * "pink screen, tight loop" entry in docs/kb/KNOWN_ISSUES.md. */
    {"ms0515-roma-original.rom", "test_omega.dsk"},
    /* RT-15SJ (Rodionov) was authored for ROM-A; with ROM-B the boot
     * stalls right after printing "НГМД готов..." — same behaviour as
     * the original 065_full.dsk on ROM-B, so this is a property of the
     * disk image, not our trimmed fixture. */
    {"ms0515-romb.rom", "test_rod.dsk"},
};

static bool isKnownBad(const std::string &rom, const std::string &disk)
{
    return kKnownBad.count({rom, disk}) > 0;
}

/* ── Boot helpers ───────────────────────────────────────────────────────── */

struct BootResult {
    bool     halted;
    bool     waiting;
    uint16_t pc;
    uint16_t psw;
    int      framesRun;
    bool     vramPopulated;    /* any non-zero byte in VRAM */
    bool     kbdInitialised;   /* USART init_step advanced */
    bool     tightLoop;        /* PC did not change over last N frames */
    bool     reachedPrompt;    /* a row starts with '.' (the cmd prompt) */
    uint8_t  borderColor;      /* 0..7; recorded for diagnosis only */
};

static BootResult runBoot(const std::string &romPath,
                          const std::string &diskPath,
                          int frames)
{
    /* TempDisk first → destructed last (after Emulator releases its
     * FILE handle) so the temp copy is always cleaned up.  The
     * fixture in tests/disks/ stays pristine even if the OS would
     * try to write to the disk during boot. */
    std::optional<ms0515_test::TempDisk> td;
    if (!diskPath.empty())
        td.emplace(diskPath);

    ms0515::Emulator emu;

    if (!emu.loadRomFile(romPath))
        return {true, false, 0, 0, 0, false, false, false, false, 0};

    if (td) {
        const auto pathStr = td->path().string();
        (void)emu.mountDisk(0, pathStr);
        /* For double-sided images (819200 bytes = 2 × FDC_DISK_SIZE)
         * also mount the upper-side unit so the OS can access side 1.
         * Required e.g. by the Rodionov RT-15SJ fixture whose copy
         * protection reads from FD2 sector 3. */
        std::error_code ec;
        const auto sz = fs::file_size(td->path(), ec);
        if (!ec && sz == 2 * 409600u)
            (void)emu.mountDisk(2, pathStr);
    }

    emu.reset();

    /* Run frames, sampling PC periodically to detect tight loops. */
    uint16_t prevPc = emu.cpu().r[CPU_REG_PC];
    int sameCount = 0;

    for (int i = 0; i < frames; i++) {
        (void)emu.stepFrame();

        /* Sample every 10 frames to detect stalls */
        if (i % 10 == 9) {
            uint16_t curPc = emu.cpu().r[CPU_REG_PC];
            if (curPc == prevPc)
                sameCount++;
            else
                sameCount = 0;
            prevPc = curPc;
        }
    }

    BootResult r{};
    r.halted     = emu.cpu().halted;
    r.waiting    = emu.cpu().waiting;
    r.pc         = emu.cpu().r[CPU_REG_PC];
    r.psw        = emu.cpu().psw;
    r.framesRun  = frames;
    r.tightLoop  = (sameCount >= 5);

    /* Check VRAM has some content (not all zeros) */
    const uint8_t *vram = board_get_vram(&emu.board());
    r.vramPopulated = false;
    for (int i = 0; i < MEM_VRAM_SIZE; i++) {
        if (vram[i] != 0) {
            r.vramPopulated = true;
            break;
        }
    }

    /* Check keyboard USART was initialised */
    r.kbdInitialised = (emu.board().kbd.init_step > 0);

    r.borderColor = emu.borderColor();

    /* Did the OS reach a command prompt?  The keyboard monitors of
     * OSA, Omega and Mihin all use '.' as the prompt character, so
     * "any row whose first non-blank glyph is '.'" is a reliable
     * signal that boot finished and is waiting for input.  Failed
     * boots (pink-screen Omega on the unpatched ROM-A, etc.) sit
     * with the BIOS banner only and never produce a '.'. */
    {
        ms0515::ScreenReader sr;
        sr.buildFont({emu.board().mem.rom, MEM_ROM_SIZE});
        auto snap = sr.readScreen({vram, MEM_VRAM_SIZE}, emu.isHires());
        r.reachedPrompt = false;
        for (int row = 0; row < ms0515::ScreenReader::kRows; ++row) {
            const auto rs = snap.row(row);
            if (!rs.empty() && rs[0] == '.') {
                r.reachedPrompt = true;
                break;
            }
        }
    }

    return r;
}

/* How many frames to boot.  At 50 Hz, 600 frames = 12 seconds of
 * emulated time — enough for POST + disk boot to reach a command
 * prompt on all known good combinations. */
static constexpr int kBootFrames = 600;

/* Frames for POST-only test (no disk, just ROM self-test). */
static constexpr int kPostFrames = 100;

/* ── POST tests: every ROM boots without disk ───────────────────────────── */

TEST_CASE("POST: ROMs boot without disk") {
    auto roms = discoverFiles(kRomDir, ".rom");
    REQUIRE_MESSAGE(!roms.empty(), "No ROM files found in " << kRomDir);

    for (const auto &romFile : roms) {
        SUBCASE(romFile.c_str()) {
            auto r = runBoot(kRomDir + "/" + romFile, {}, kPostFrames);

            CHECK_MESSAGE(!r.halted,
                "CPU halted at PC=0" << std::oct << r.pc);
            CHECK(r.vramPopulated);
            CHECK(r.kbdInitialised);
        }
    }
}

/* ── Disk boot tests: every ROM × every disk ────────────────────────────── */

TEST_CASE("Boot: ROM + disk matrix") {
    auto roms  = discoverFiles(kRomDir, ".rom");
    auto disks = discoverFiles(kDiskDir, ".dsk");
    REQUIRE_MESSAGE(!roms.empty(),  "No ROM files found in "  << kRomDir);
    REQUIRE_MESSAGE(!disks.empty(), "No disk files found in " << kDiskDir);

    for (const auto &romFile : roms) {
        SUBCASE(romFile.c_str()) {
            for (const auto &diskFile : disks) {
                SUBCASE(diskFile.c_str()) {
                    auto r = runBoot(kRomDir  + "/" + romFile,
                                     kDiskDir + "/" + diskFile,
                                     kBootFrames);

                    if (isKnownBad(romFile, diskFile)) {
                        WARN_MESSAGE(!r.halted,
                            "[known-bad] CPU halted at PC=0"
                            << std::oct << r.pc);
                        WARN_MESSAGE(!r.tightLoop,
                            "[known-bad] CPU stuck at PC=0"
                            << std::oct << r.pc);
                        WARN(r.vramPopulated);
                        WARN_MESSAGE(r.reachedPrompt,
                            "[known-bad] no '.' prompt visible after "
                            << kBootFrames << " frames");
                    } else {
                        CHECK_MESSAGE(!r.halted,
                            "CPU halted at PC=0" << std::oct << r.pc);
                        CHECK_MESSAGE(!r.tightLoop,
                            "CPU stuck in tight loop at PC=0"
                            << std::oct << r.pc);
                        CHECK(r.vramPopulated);
                        CHECK_MESSAGE(r.reachedPrompt,
                            "no '.' prompt visible after "
                            << kBootFrames
                            << " frames — boot did not reach a usable "
                            "state");
                    }
                }
            }
        }
    }
}

/* ── Pink-screen diagnostic ──────────────────────────────────────────────── */

/* Skip-marked: only enabled with --no-skip when actively investigating
 * the Omega-on-unpatched-ROM-A stall.  Captures PC trajectory at coarse
 * intervals and disassembles the neighbourhood of the final stuck PC. */
TEST_CASE("DIAG pink-screen: trace PC through Omega + roma-original" * doctest::skip()) {
    const std::string romPath = kRomDir  + "/ms0515-roma-original.rom";
    const std::string diskPath = kDiskDir + "/test_omega.dsk";

    if (!fs::exists(romPath) || !fs::exists(diskPath)) {
        std::fprintf(stderr, "[diag] missing rom or disk fixture, skipping\n");
        return;
    }

    ms0515_test::TempDisk td{diskPath};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(romPath));
    (void)emu.mountDisk(0, td.path().string());
    /* Double-side mount — Omega disk is 819200 bytes. */
    std::error_code ec;
    if (fs::file_size(td.path(), ec) == 2 * 409600u)
        (void)emu.mountDisk(2, td.path().string());
    emu.reset();

    constexpr int kFrames    = 3000;
    constexpr int kSampleEvery = 200;

    std::vector<uint16_t> pcSamples;
    pcSamples.reserve(kFrames / kSampleEvery + 1);

    for (int i = 0; i < kFrames; ++i) {
        (void)emu.stepFrame();
        if (i % kSampleEvery == 0) {
            pcSamples.push_back(emu.cpu().r[CPU_REG_PC]);
        }
    }

    const uint16_t finalPc = emu.cpu().r[CPU_REG_PC];
    const uint16_t sp      = emu.cpu().r[CPU_REG_SP];
    const uint16_t psw     = emu.cpu().psw;

    std::fprintf(stderr, "\n[diag] after %d frames:\n", kFrames);
    std::fprintf(stderr, "[diag]   PC=%06o  SP=%06o  PSW=%06o  halted=%d  waiting=%d\n",
                 finalPc, sp, psw,
                 (int)emu.cpu().halted, (int)emu.cpu().waiting);
    std::fprintf(stderr, "[diag]   R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o\n",
                 emu.cpu().r[0], emu.cpu().r[1], emu.cpu().r[2],
                 emu.cpu().r[3], emu.cpu().r[4], emu.cpu().r[5]);

    std::fprintf(stderr, "[diag] PC samples (every %d frames):\n", kSampleEvery);
    for (size_t i = 0; i < pcSamples.size(); ++i) {
        std::fprintf(stderr, "[diag]   frame %4zu: PC=%06o\n",
                     i * kSampleEvery, pcSamples[i]);
    }

    /* Disassemble 16 instructions around the final PC to see the loop. */
    std::fprintf(stderr, "[diag] disassembly around final PC:\n");
    auto reader = [&emu](uint16_t a) -> uint16_t {
        return const_cast<ms0515::Emulator&>(emu).readWord(a);
    };
    uint16_t scan = finalPc > 32 ? (uint16_t)(finalPc - 32) : 0;
    scan &= ~1u;
    for (int i = 0; i < 30; ++i) {
        auto d = ms0515::Disassembler::decode(scan, reader);
        std::fprintf(stderr, "[diag]   %06o:  %s%s\n",
                     d.address,
                     d.mnemonic.c_str(),
                     d.operands.empty() ? "" : ("\t" + d.operands).c_str());
        if (d.length == 0) break;
        scan = (uint16_t)(d.address + d.length);
    }

    /* Also dump the routine entry: 162516 is INC R1, but where was it
     * called from?  Try walking the stack a few frames. */
    std::fprintf(stderr, "[diag] stack (top 8 words):\n");
    for (int i = 0; i < 8; ++i) {
        uint16_t a = (uint16_t)(sp + i*2);
        std::fprintf(stderr, "[diag]   SP+%d (%06o): %06o\n",
                     i*2, a, reader(a));
    }

    /* Disassemble a wider scan upward from finalPc (50 bytes back) to
     * find the routine header. */
    std::fprintf(stderr, "[diag] wider context above final PC:\n");
    scan = (uint16_t)(finalPc - 60) & ~1u;
    for (int i = 0; i < 20; ++i) {
        auto d = ms0515::Disassembler::decode(scan, reader);
        std::fprintf(stderr, "[diag]   %06o:  %s%s\n",
                     d.address,
                     d.mnemonic.c_str(),
                     d.operands.empty() ? "" : ("\t" + d.operands).c_str());
        if (d.length == 0) break;
        scan = (uint16_t)(d.address + d.length);
    }

    /* Disassemble forward from final PC to find exit conditions
     * (TST R3, magic-byte CMPs, etc.) */
    std::fprintf(stderr, "[diag] forward context from final PC:\n");
    scan = finalPc;
    for (int i = 0; i < 40; ++i) {
        auto d = ms0515::Disassembler::decode(scan, reader);
        std::fprintf(stderr, "[diag]   %06o:  %s%s\n",
                     d.address,
                     d.mnemonic.c_str(),
                     d.operands.empty() ? "" : ("\t" + d.operands).c_str());
        if (d.length == 0) break;
        scan = (uint16_t)(d.address + d.length);
    }

    /* Disassemble the caller (return address from stack top). */
    uint16_t retAddr = reader(sp);
    std::fprintf(stderr, "[diag] caller context (return PC %06o):\n", retAddr);
    scan = retAddr > 24 ? (uint16_t)(retAddr - 24) : 0;
    scan &= ~1u;
    for (int i = 0; i < 25; ++i) {
        auto d = ms0515::Disassembler::decode(scan, reader);
        std::fprintf(stderr, "[diag]   %06o:  %s%s\n",
                     d.address,
                     d.mnemonic.c_str(),
                     d.operands.empty() ? "" : ("\t" + d.operands).c_str());
        if (d.length == 0) break;
        scan = (uint16_t)(d.address + d.length);
    }
}

} /* TEST_SUITE */
