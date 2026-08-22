/*
 * FistGame.hpp - the harness the FIST port's tests run on.
 *
 * Boots RT-11 headlessly with the built game on a folder device (boot/ =
 * the system template + FIST.SAV + STARTS.COM auto-running "R FIST", work/ =
 * GST.DAT as DK), feeds the date prompts, and exposes the game's state: the
 * $9C00.. game-state block lives in the extended banks 12-14, VRAM through
 * board_get_vram().  Every path has a default under the repo (overridable
 * with --fist-<name>=... options, see test_main.cpp), and the suite skips
 * itself when the game is not built - the artifacts embed the original's art
 * and are never committed.
 */
#pragma once

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include "EmulatorInternal.hpp"
extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
#include <ms0515/core/memory.h>
}

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fist {

namespace fs = std::filesystem;

/* Harness options: --fist-<name>=<value> on the command line (test_main.cpp). */
inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> opts;
    return opts;
}
inline std::string opt(const char *name)
{
    auto it = options().find(name);
    return it == options().end() ? std::string{} : it->second;
}
inline std::string optOr(const char *name, const std::string &dflt)
{
    std::string v = opt(name);
    return v.empty() ? dflt : v;
}

inline std::string savPath()    { return optOr("sav", std::string{FIST_DIR} + "/FIST.SAV"); }
inline std::string datPath()    { return optOr("dat", std::string{FIST_DIR} + "/GST.DAT"); }
inline std::string systemDir()  { return optOr("system", std::string{FIST_SYSTEM_DIR}); }
inline std::string dskPath()    { return optOr("dsk", std::string{FIST_DSK}); }
inline std::string expectDir()  { return optOr("expect", std::string{FIST_DIR}); }

/* The game is built (FIST.SAV + GST.DAT present)?  Tests skip otherwise. */
inline bool built()
{
    return fs::exists(savPath()) && fs::exists(datPath()) && fs::exists(systemDir());
}

inline std::vector<uint8_t> readFile(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

inline void writeFile(const fs::path &p, const void *data, size_t n)
{
    std::ofstream o(p, std::ios::binary);
    o.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
}

class FistGame {
public:
    ms0515::Emulator emu;

    /* Stage the folder devices under %TEMP%/<name>, mount, boot the game:
     * the loading screen shows during the load, the attract demo follows. */
    explicit FistGame(const char *name, int bootFrames = 1200)
    {
        stage(name);
        REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
        emu.reset();
        REQUIRE(emu.mountDisk(0, (boot_ / "device.rtfs").string()));
        REQUIRE(emu.mountDisk(1, (work_ / "device.rtfs").string()));
        hookConsole();
        bootUp(bootFrames);
    }

    /* A real .dsk on disk 0 (the GUI's configuration). */
    explicit FistGame(const std::string &dsk, int bootFrames)
    {
        REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
        emu.reset();
        REQUIRE(emu.mountDisk(0, dsk));
        hookConsole();
        bootUp(bootFrames);
    }

    ms0515_board_t &board() { return ms0515::internal::board(emu); }
    ms0515_cpu_t   &cpu()   { return ms0515::internal::cpu(emu); }

    /* Game-state cells: Spectrum $9C00.. is the extended banks 12-14. */
    uint8_t gst(uint16_t spec)
    {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        return board().mem.ram[((addr >> 13) + 8) * 8192 + (addr & 8191)];
    }
    void poke(uint16_t spec, uint8_t v)
    {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        board().mem.ram[((addr >> 13) + 8) * 8192 + (addr & 8191)] = v;
    }

    /* One video frame with the keyboard's clock ticking (auto-repeat) and,
     * when P2 is parked, its move held at idle. */
    void step()
    {
        if (frame_ < 900 && frame_ % 30 == 0) offerCR_ = true;   // the date prompts
        if (parked_) poke(0xAA45, 1);
        nowMs_ += 20;
        emu.keyTick(nowMs_);
        (void)emu.stepFrame();
        ++frame_;
    }
    void settle(int n) { for (int i = 0; i < n; ++i) step(); }
    int frame() const { return frame_; }

    void keyTap(ms0515::Key k, int holdFrames = 5)
    {
        emu.keyPress(k, true);
        settle(holdFrames);
        emu.keyPress(k, false);
    }

    /* Leave the attract demo: fire starts the 1-player game ($97E3). */
    /* Fire from the demo -> the 1UP game.  The sound flag ($B2FA) is cleared
     * first: the opponent set-up plays the original's tune otherwise (~3.5 s
     * of blocking beeper), which no test needs - the sound diagnostic sets
     * the flag back. */
    void startGame()
    {
        poke(0xB2FA, 0);
        settle(5);                                  // a game frame stashes it
        keyTap(ms0515::Key::Space);
        settle(60);
    }

    /* Take P2 off the AI and hold it idle (MOVSEL's human branch leaves $AA45
     * alone, so the AI's last move would stay latched otherwise). */
    void parkP2()
    {
        poke(0xAA46, 0);
        parked_ = true;
    }

    const uint8_t *vram() { return board_get_vram(&board()); }
    int vramNonzero()
    {
        const uint8_t *v = vram();
        int n = 0;
        for (int i = 0; i < MEM_VRAM_SIZE; ++i) if (v[i]) ++n;
        return n;
    }
    void dumpVram(const fs::path &p) { writeFile(p, vram(), MEM_VRAM_SIZE); }

    /* Put both fighters into a known idle state: start positions, P1 facing
     * right (a somersault or a spinning kick flips it), a full clock. */
    void resetFighters()
    {
        poke(0xAA19, 40); poke(0xAA59, 76); poke(0x9CA5, 30);
        poke(0xAA17, 0); poke(0xAA57, 1);
        poke(0xAA04, 1); poke(0xAA05, 1);
    }

private:
    fs::path boot_, work_;
    bool offerCR_ = false;
    bool parked_ = false;
    uint32_t nowMs_ = 0;
    int frame_ = 0;

    void stage(const char *name)
    {
        fs::path tmp = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(tmp, ec);
        boot_ = tmp / "boot";
        work_ = tmp / "work";
        fs::create_directories(boot_, ec);
        fs::copy(systemDir(), boot_, fs::copy_options::recursive, ec);
        REQUIRE_FALSE(ec);
        fs::copy_file(savPath(), boot_ / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
        const char starts[] = "ASSIGN DZ1 DK\r\nR FIST\r\n";
        writeFile(boot_ / "STARTS.COM", starts, sizeof starts - 1);
        fs::create_directories(work_, ec);
        fs::copy_file(datPath(), work_ / "GST.DAT", fs::copy_options::overwrite_existing, ec);
        const char rtfs[] = "device: floppy\nblocks: 800\n";
        writeFile(work_ / "device.rtfs", rtfs, sizeof rtfs - 1);
    }

    /* RT-11's console is the serial port: answer the date/time prompts with
     * CRs, paced so they land on the prompts, not flood the input. */
    void hookConsole()
    {
        emu.setSerialCallbacks(
            [this](uint8_t &b) -> bool {
                if (offerCR_) { b = '\r'; offerCR_ = false; return true; }
                return false;
            },
            [](uint8_t) -> bool { return true; });
    }

    void bootUp(int frames) { settle(frames); }
};

}  // namespace fist
