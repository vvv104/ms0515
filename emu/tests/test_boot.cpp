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
 * Returns filenames sorted alphabetically for stable ordering.
 * Optionally filter by filename prefix so the boot suite picks up
 * only system ROMs, not peripheral firmware that lives in the same
 * directory (e.g. the MS7004 keyboard's i8035 image). */
static std::vector<std::string> discoverFiles(const std::string &dir,
                                              const std::string &ext,
                                              const std::string &prefix = "")
{
    std::vector<std::string> result;
    if (!fs::is_directory(dir))
        return result;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ext)
            continue;
        const auto name = entry.path().filename().string();
        if (!prefix.empty() && name.rfind(prefix, 0) != 0)
            continue;
        result.push_back(name);
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
    auto roms = discoverFiles(kRomDir, ".rom", "ms0515-");
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
    auto roms  = discoverFiles(kRomDir, ".rom", "ms0515-");
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

} /* TEST_SUITE */
