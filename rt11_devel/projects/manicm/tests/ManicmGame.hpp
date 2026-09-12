/*
 * ManicmGame.hpp - the harness the MANICM port's tests run on.
 *
 * Boots RT-11 headlessly with the built game on a folder device (the
 * toolset's system template + MANICM.SAV + MANICM.DAT + a STARTS.COM that
 * runs it), answers the date prompts, and reads the game's state where the
 * link map says it is: MANICM.MAP names CAVNUM, LIVES, MODE, CHEATC, SCORE,
 * WILLYA, WILLYY, AIRBRN.  Every path has a default under the repo
 * (overridable with --manicm-<name>=... options, see test_main.cpp), and
 * the suite skips itself when the game is not built - the data file is the
 * original's and is never committed.
 */
#pragma once

#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include "EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/memory.h>
}

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace manicm {

namespace fs = std::filesystem;

/* Harness options: --manicm-<name>=<value> on the command line. */
inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> opts;
    return opts;
}

inline std::string optOr(const char *name, const std::string &dflt)
{
    auto it = options().find(name);
    return it == options().end() || it->second.empty() ? dflt : it->second;
}

inline std::string savPath()   { return optOr("sav", std::string{MANICM_DIR} + "/MANICM.SAV"); }
inline std::string datPath()   { return optOr("dat", std::string{MANICM_DIR} + "/MANICM.DAT"); }
inline std::string mapPath()   { return optOr("map", std::string{MANICM_DIR} + "/MANICM.MAP"); }
inline std::string systemDir() { return optOr("system", std::string{MANICM_SYSTEM_DIR}); }

/* The game is built?  Tests skip otherwise. */
inline bool built()
{
    return fs::exists(savPath()) && fs::exists(datPath()) && fs::exists(mapPath()) && fs::exists(systemDir());
}

inline std::string readText(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

inline void writeFile(const fs::path &p, const void *data, size_t n)
{
    std::ofstream o(p, std::ios::binary);
    o.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
}

/* The link map's globals: "NAME  013376" pairs, octal. */
inline std::map<std::string, uint16_t> symbols()
{
    std::map<std::string, uint16_t> out;
    const std::string text = readText(mapPath());
    static const std::regex pair(R"(([A-Z][A-Z0-9$.]{0,5})\s+([0-7]{6}))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pair); it != std::sregex_iterator(); ++it)
        out[(*it)[1].str()] = static_cast<uint16_t>(std::stoul((*it)[2].str(), nullptr, 8));
    return out;
}

class ManicmGame {
public:
    ms0515::Emulator emu;

    /* Stage the folder device under %TEMP%/<name>, mount it, boot into the
     * game: RT-11's prompts answered, "R MANICM" run by STARTS.COM, the
     * title screen up by the end of `bootFrames`. */
    explicit ManicmGame(const char *name, int bootFrames = 1500)
    {
        syms_ = symbols();
        REQUIRE(syms_.count("CAVNUM"));
        stage(name);
        REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
        emu.reset();
        REQUIRE(emu.mountDisk(0, (boot_ / "device.rtfs").string()));
        hookConsole();
        settle(bootFrames);
    }

    ms0515_board_t &board() { return ms0515::internal::board(emu); }

    uint16_t sym(const char *name) const
    {
        auto it = syms_.find(name);
        REQUIRE_MESSAGE(it != syms_.end(), name);
        return it->second;
    }
    /* The program lives in the primary banks below 30000: RAM as addressed. */
    uint8_t  peek8(const char *name, int off = 0)  { return board().mem.ram[sym(name) + off]; }
    uint16_t peek16(const char *name, int off = 0)
    {
        const uint8_t *p = &board().mem.ram[sym(name) + off];
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    /* One video frame, the keyboard's clock ticking (auto-repeat). */
    void step()
    {
        if (frame_ < 900 && frame_ % 30 == 0) offerCR_ = true;   // the date prompts
        nowMs_ += 20;
        emu.keyTick(nowMs_);
        (void)emu.stepFrame();
        ++frame_;
    }
    void settle(int n) { for (int i = 0; i < n; ++i) step(); }
    int frame() const { return frame_; }

    void keyTap(ms0515::Key k, int holdFrames = 6)
    {
        emu.keyPress(k, true);
        settle(holdFrames);
        emu.keyPress(k, false);
    }

    /* ENTER at the title: the game, Central Cavern. */
    void startGame()
    {
        keyTap(ms0515::Key::Return, 8);
        settle(40);
    }

    const uint8_t *vram() { return board_get_vram(&board()); }
    int vramNonzero()
    {
        const uint8_t *v = vram();
        int n = 0;
        for (int i = 0; i < MEM_VRAM_SIZE; ++i) if (v[i]) ++n;
        return n;
    }

private:
    fs::path boot_;
    bool offerCR_ = false;
    uint32_t nowMs_ = 0;
    int frame_ = 0;
    std::map<std::string, uint16_t> syms_;

    void stage(const char *name)
    {
        fs::path tmp = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(tmp, ec);
        boot_ = tmp / "boot";
        fs::create_directories(boot_, ec);
        fs::copy(systemDir(), boot_, fs::copy_options::recursive, ec);
        REQUIRE_FALSE(ec);
        fs::copy_file(savPath(), boot_ / "MANICM.SAV", fs::copy_options::overwrite_existing, ec);
        fs::copy_file(datPath(), boot_ / "MANICM.DAT", fs::copy_options::overwrite_existing, ec);
        const char starts[] = "R MANICM\r\n";
        writeFile(boot_ / "STARTS.COM", starts, sizeof starts - 1);
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
};

}  // namespace manicm
