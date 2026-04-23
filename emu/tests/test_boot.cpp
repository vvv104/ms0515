/*
 * test_boot.cpp — Boot smoke tests.
 *
 * Loads real ROM + disk combinations and verifies the system boots
 * successfully: no HALT, no tight loop, VRAM populated, peripherals
 * initialised.
 *
 * ROMs are discovered from ASSETS_DIR/rom/*.rom, disks from
 * ASSETS_DIR/disks/*.dsk.  Adding a new ROM or disk image to the
 * corresponding directory automatically creates new test cases.
 *
 * These are integration tests that exercise the entire stack:
 * CPU → memory → board → timer → keyboard → FDC.
 */

#include <doctest/doctest.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Emulator.hpp>

namespace fs = std::filesystem;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

TEST_SUITE("Boot") {

/* ── Asset discovery ────────────────────────────────────────────────────── */

static const std::string kAssetsDir = ASSETS_DIR;
static const std::string kRomDir    = kAssetsDir + "/rom";
static const std::string kDiskDir   = kAssetsDir + "/disks";

/* Collect all files matching an extension in a directory.
 * Returns filenames sorted alphabetically for stable ordering. */
static std::vector<std::string> discoverFiles(const std::string &dir,
                                              const std::string &ext)
{
    std::vector<std::string> result;
    if (!fs::is_directory(dir))
        return result;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            result.push_back(entry.path().filename().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

/* ── Known-bad combinations ─────────────────────────────────────────────── */

/* ROM+disk pairs that are known to fail.  These are tested with WARN
 * instead of CHECK so the suite stays green while the issues are
 * documented.  See also: assets/KNOWN_ISSUES.md */
static const std::set<std::pair<std::string, std::string>> kKnownBad = {
    {"ms0515-roma-original.rom", "omega-lang.dsk"},
    /* rodionov.dsk is copy-protected and needs rodionov2.dsk on FD2 plus
     * the patched ms0515-roma.rom.  The boot matrix mounts only FD0, so
     * every ROM × rodionov*.dsk combination halts or tight-loops here.
     * See docs/kb/KNOWN_ISSUES.md. */
    {"ms0515-roma.rom",          "rodionov.dsk"},
    {"ms0515-roma-original.rom", "rodionov.dsk"},
    {"ms0515-romb.rom",          "rodionov.dsk"},
    {"ms0515-roma.rom",          "rodionov2.dsk"},
    {"ms0515-roma-original.rom", "rodionov2.dsk"},
    {"ms0515-romb.rom",          "rodionov2.dsk"},
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
};

static BootResult runBoot(const std::string &romPath,
                          const std::string &diskPath,
                          int frames)
{
    ms0515::Emulator emu;

    if (!emu.loadRomFile(romPath))
        return {true, false, 0, 0, 0, false, false, false};

    if (!diskPath.empty())
        (void)emu.mountDisk(0, diskPath);

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

    return r;
}

/* How many frames to boot.  At 50 Hz, 200 frames = 4 seconds of
 * emulated time — enough for POST + disk boot on all known ROMs. */
static constexpr int kBootFrames = 200;

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
                    } else {
                        CHECK_MESSAGE(!r.halted,
                            "CPU halted at PC=0" << std::oct << r.pc);
                        CHECK_MESSAGE(!r.tightLoop,
                            "CPU stuck in tight loop at PC=0"
                            << std::oct << r.pc);
                        CHECK(r.vramPopulated);
                    }
                }
            }
        }
    }
}

} /* TEST_SUITE */
