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
    CHECK(nz > 500);
}
