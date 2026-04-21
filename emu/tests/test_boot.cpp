/*
 * test_boot.cpp — Boot smoke tests.
 *
 * Loads real ROM + disk combinations and verifies the system boots
 * successfully: no HALT, no tight loop, VRAM populated, peripherals
 * initialised.  Tests are skipped if the required asset files are
 * not found (they are not tracked in git).
 *
 * These are integration tests that exercise the entire stack:
 * CPU → memory → board → timer → keyboard → FDC.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" {
#include <ms0515/board.h>
}

#include <ms0515/Emulator.hpp>

namespace fs = std::filesystem;

TEST_SUITE("Boot") {

/* ── Asset paths ─────────────────────────────────────────────────────────── */

/* Try several locations: the tests may be run from the build dir, the
 * repo root, or the package dir.  Return empty string if not found. */
static std::string findAsset(const std::string &relative)
{
    static const std::string roots[] = {
        "../../assets/",            /* from build/Release/   */
        "../assets/",               /* from build/           */
        "assets/",                  /* from emu/             */
        "../../package/assets/",    /* package dir           */
        "../package/assets/",
        "package/assets/",
    };
    for (const auto &root : roots) {
        std::string path = root + relative;
        if (fs::exists(path))
            return path;
    }
    return {};
}

static std::string findRom(const std::string &name)
{
    return findAsset("rom/" + name);
}

static std::string findDisk(const std::string &name)
{
    return findAsset("disks/" + name);
}

/* ── Boot helpers ────────────────────────────────────────────────────────── */

struct BootResult {
    bool     halted;
    bool     waiting;
    uint16_t pc;
    uint16_t psw;
    int      framesRun;
    bool     vramPopulated;    /* any non-zero byte in VRAM */
    bool     timerActive;      /* at least one channel programmed */
    bool     kbdInitialised;   /* USART init_step advanced */
    bool     tightLoop;        /* PC did not change over last N frames */
};

static BootResult runBoot(const std::string &romPath,
                          const std::string &diskPath,
                          int frames)
{
    ms0515::Emulator emu;

    if (!emu.loadRomFile(romPath))
        return {true, false, 0, 0, 0, false, false, false, false};

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

    /* Check timer was programmed (any channel with count loaded) */
    r.timerActive = false;
    for (int ch = 0; ch < 3; ch++) {
        if (!timer_get_out(&emu.board().timer, ch)) {
            r.timerActive = true;  /* OUT low = channel was programmed */
            break;
        }
    }
    /* Also check if timer channel 0 OUT toggles (rate generator for USART) */
    if (!r.timerActive) {
        bool out_before = timer_get_out(&emu.board().timer, 0);
        for (int i = 0; i < 100; i++)
            timer_tick(&const_cast<ms0515_board_t &>(emu.board()).timer);
        bool out_after = timer_get_out(&emu.board().timer, 0);
        if (out_before != out_after)
            r.timerActive = true;
    }

    /* Check keyboard USART was initialised */
    r.kbdInitialised = (emu.board().kbd.init_step > 0);

    return r;
}

/* How many frames to boot.  At 50 Hz, 200 frames = 4 seconds of
 * emulated time — enough for POST + disk boot on all known ROMs. */
static constexpr int kBootFrames = 200;

/* ── POST-only tests (no disk) ───────────────────────────────────────────── */

TEST_CASE("POST: ROM-A boots without disk") {
    auto rom = findRom("ms0515-roma.rom");
    if (rom.empty()) { MESSAGE("SKIP: ms0515-roma.rom not found"); return; }

    auto r = runBoot(rom, {}, 100);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
    CHECK(r.kbdInitialised);
}

TEST_CASE("POST: ROM-A original boots without disk") {
    auto rom = findRom("ms0515-roma-original.rom");
    if (rom.empty()) { MESSAGE("SKIP: ms0515-roma-original.rom not found"); return; }

    auto r = runBoot(rom, {}, 100);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
    CHECK(r.kbdInitialised);
}

TEST_CASE("POST: ROM-B boots without disk") {
    auto rom = findRom("ms0515-romb.rom");
    if (rom.empty()) { MESSAGE("SKIP: ms0515-romb.rom not found"); return; }

    auto r = runBoot(rom, {}, 100);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
    CHECK(r.kbdInitialised);
}

/* ── Disk boot tests ─────────────────────────────────────────────────────── */

TEST_CASE("Boot: ROM-A + OSA") {
    auto rom  = findRom("ms0515-roma.rom");
    auto disk = findDisk("osa.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

TEST_CASE("Boot: ROM-A + Omega Games") {
    auto rom  = findRom("ms0515-roma.rom");
    auto disk = findDisk("omega-games.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

TEST_CASE("Boot: ROM-A + Omega Lang") {
    auto rom  = findRom("ms0515-roma.rom");
    auto disk = findDisk("omega-lang.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

TEST_CASE("Boot: ROM-B + OSA") {
    auto rom  = findRom("ms0515-romb.rom");
    auto disk = findDisk("osa.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

TEST_CASE("Boot: ROM-B + Omega Games") {
    auto rom  = findRom("ms0515-romb.rom");
    auto disk = findDisk("omega-games.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

TEST_CASE("Boot: ROM-B + Omega Lang") {
    auto rom  = findRom("ms0515-romb.rom");
    auto disk = findDisk("omega-lang.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

/* ── Known-bad: Mihin halts (regression marker) ─────────────────────────── */

TEST_CASE("Boot: ROM-A + Mihin (known halt at 0157406)") {
    auto rom  = findRom("ms0515-roma.rom");
    auto disk = findDisk("mihin.dsk");
    if (rom.empty() || disk.empty()) {
        MESSAGE("SKIP: ROM or disk not found");
        return;
    }

    auto r = runBoot(rom, disk, kBootFrames);

    /* Mihin previously halted at 0157406.  If this starts failing,
     * the boot regression has returned. */
    CHECK_MESSAGE(!r.halted, "CPU halted at PC=0" << std::oct << r.pc);
    CHECK_MESSAGE(!r.tightLoop, "CPU stuck in tight loop at PC=0" << std::oct << r.pc);
    CHECK(r.vramPopulated);
}

} /* TEST_SUITE */
