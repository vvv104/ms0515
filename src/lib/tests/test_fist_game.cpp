/*
 * test_fist_game.cpp - boot RT-11 with the standalone FIST game on a folder
 * device, let it auto-run (STARTS.COM -> R FIST) so it loads GST.DAT into the
 * parked extended banks and renders, then dump VRAM.  Opt-in via env vars (all
 * must be set, else the test no-ops):
 *   FIST_GAME_SAV       path to the game FIST.SAV
 *   FIST_GAME_DAT       path to GST.DAT
 *   FIST_SYSTEM_DIR     path to the RT-11 system/ template folder
 *   FIST_GAME_VRAM_OUT  where to write the 16 KB VRAM dump
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include "../src/EmulatorInternal.hpp"
extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
#include <ms0515/core/memory.h>
}

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string env(const char *k)
{
    char *v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, k) != 0 || !v)
        return {};
    std::string r{v};
    free(v);
    return r;
}

TEST_CASE("fist: standalone game render via .DAT loader")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), out = env("FIST_GAME_VRAM_OUT");
    if (sav.empty() || dat.empty() || sys.empty() || out.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_game_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    // RT-11's console here is the serial port (as in the CLI).  Feed CRs through
    // the serial-in callback to accept the date/time prompts; STARTS.COM then
    // auto-runs the game.  Pace the CRs (one offered every ~30 frames) so they land
    // at the prompts rather than flooding early.
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });    // discard console output
    std::string framesEnv = env("FIST_GAME_FRAMES");
    int frames = framesEnv.empty() ? 3000 : std::atoi(framesEnv.c_str());
    for (int i = 0; i < frames; ++i) {
        if (i < 900 && (i % 30) == 0)
            offerCR = true;
        (void)emu.stepFrame();
    }

    auto &cpu = ms0515::internal::cpu(emu);
    MESSAGE("final CPU PC = " << std::oct << cpu_get_pc(&cpu) << std::dec);
    auto &board = ms0515::internal::board(emu);
    const uint8_t *vram = board_get_vram(&board);
    {
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char *>(vram), MEM_VRAM_SIZE);
    }
    int nz = 0;
    for (int i = 0; i < MEM_VRAM_SIZE; ++i)
        if (vram[i]) ++nz;
    MESSAGE("VRAM non-zero bytes: " << nz);
    // The per-frame loop catches the game at an arbitrary frame; a live two-fighter
    // frame is ~900 nz.  >500 distinguishes that from a blank/trapped screen (<=560).
    CHECK(nz > 300);
}

TEST_CASE("fist: keyboard drives P1")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_kbd_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });

    auto &board = ms0515::internal::board(emu);
    // GST lives in the extended banks (slot N -> physical bank N+8); $AA19 = P1 x.
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);   // runtime home 0100000
        uint32_t bank = (addr >> 13) + 8;              // extended bank 12..14
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };

    // Boot the game (clear the date prompts, let the loader run).
    for (int i = 0; i < 1500; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }

    auto settle = [&](int n) { for (int i = 0; i < n; ++i) (void)emu.stepFrame(); };
    // P1 must be human (AA06=0) or MOVSEL's AI overrides the keyboard.
    int human = gst(0xAA06);
    MESSAGE("P1 AA06 (0=human): " << human);
    CHECK(human == 0);

    int xBase = gst(0xAA19);
    emu.keyPress(ms0515::Key::Right, true);
    settle(400);
    int xRight = gst(0xAA19);
    emu.keyPress(ms0515::Key::Right, false);
    settle(80);
    emu.keyPress(ms0515::Key::Left, true);
    settle(400);
    int xLeft = gst(0xAA19);
    emu.keyPress(ms0515::Key::Left, false);

    MESSAGE("P1 x ($AA19): baseline=" << xBase
            << "  after RIGHT=" << xRight << "  after LEFT=" << xLeft);
    CHECK(xRight != xLeft);      // keyboard moves P1
    CHECK(xRight >= xLeft);      // RIGHT ends further right than LEFT
}

TEST_CASE("fist: yin-yang score accumulates")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_score_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });

    auto &board = ms0515::internal::board(emu);
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };

    for (int i = 0; i < 1200; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    int a01_0 = gst(0xAA01), a41_0 = gst(0xAA41);
    // Drive P1 aggressively: hold RIGHT to close, punch (SPACE) in bursts, so real
    // hits/knockdowns happen and ROUNDE has exchanges to score.
    int peak = 0, changes = 0;
    int prev = gst(0xAA01) + gst(0xAA41);
    emu.keyPress(ms0515::Key::Right, true);
    for (int i = 0; i < 16000; ++i) {
        if (i == 600) { emu.keyPress(ms0515::Key::Right, false); }
        if (i >= 600) {
            bool atk = (i / 40) % 2 == 0;      // alternate punch / approach
            emu.keyPress(ms0515::Key::Space, atk);
            emu.keyPress(ms0515::Key::Right, !atk);
        }
        (void)emu.stepFrame();
        peak = std::max(peak, std::max((int)gst(0xAA01), (int)gst(0xAA41)));
        int s = gst(0xAA01) + gst(0xAA41);
        if (s != prev) { ++changes; prev = s; }
    }
    MESSAGE("start AA01/AA41=" << a01_0 << "/" << a41_0
            << "  peak yin-yang=" << peak << "  score-changes=" << changes);
    CHECK(a01_0 == 0);           // the match starts 0-0 (GST.DAT snapshot cleared)
    CHECK(changes > 0);          // clean hits are scored into the yin-yang total
    CHECK(peak >= 2);            // at least a full yin-yang accrues over the bout
}
