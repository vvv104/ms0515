/*
 * TuneRun.hpp - the harness the tune's tests run on.
 *
 * Boots RT-11 headlessly with SABTUN.SAV on a folder device (the toolset's
 * system template + the program + a STARTS.COM that runs it), answers the
 * date prompts, and records every level change of the speaker with the
 * CPU cycle it happened at.  The reference is SAB2TN.REF: the model's
 * rendering of the original, one line per level change - the T-state,
 * the level and a tag naming the paths that led to it (see
 * source/engine_model.py).  Every path has a default under the repo
 * (overridable with --sab2tune-<name>=... options, see test_main.cpp),
 * and the suite skips itself when the tune is not built.
 */
#pragma once

#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include "EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/board.h>
}

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace sab2tune {

namespace fs = std::filesystem;

/* Harness options: --sab2tune-<name>=<value> on the command line. */
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

inline std::string savPath()   { return optOr("sav", std::string{SAB2TUNE_DIR} + "/SABTUN.SAV"); }
inline std::string mapPath()   { return optOr("map", std::string{SAB2TUNE_DIR} + "/SABTUN.MAP"); }
inline std::string refPath()   { return optOr("ref", std::string{SAB2TUNE_DIR} + "/SAB2TN.REF"); }
inline std::string systemDir() { return optOr("system", std::string{SAB2TUNE_SYSTEM_DIR}); }

/* The tune is built?  Tests skip otherwise. */
inline bool built()
{
    return fs::exists(savPath()) && fs::exists(refPath()) && fs::exists(systemDir());
}

inline void writeFile(const fs::path &p, const void *data, size_t n)
{
    std::ofstream o(p, std::ios::binary);
    o.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
}

inline std::string readText(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/* The link map's globals: "NAME  013376" pairs, octal - the engine's
 * variables it exports for the tests. */
inline std::map<std::string, uint16_t> symbols()
{
    std::map<std::string, uint16_t> out;
    const std::string text = readText(mapPath());
    static const std::regex pair(R"(([A-Z][A-Z0-9$.]{0,5})\s+([0-7]{6}))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pair); it != std::sregex_iterator(); ++it)
        out[(*it)[1].str()] = static_cast<uint16_t>(std::stoul((*it)[2].str(), nullptr, 8));
    return out;
}

/* One level change of the speaker. */
struct Change {
    uint64_t    at;       /* CPU cycles (ours) or T-states (the reference) */
    int         level;
    std::string tag;      /* the reference's path tag; empty for ours */
};

/* The reference: "T level tag" lines. */
inline std::vector<Change> reference()
{
    std::vector<Change> out;
    std::ifstream f(refPath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        Change c;
        in >> c.at >> c.level >> c.tag;
        out.push_back(c);
    }
    return out;
}

/* One T-state of the Spectrum in our clocks: 7.5 MHz over 3.5 MHz.  The
 * reference is rendered with this machine's frame and the engine's start
 * phase in it (source/gen_data.py: MACHINE_FRAME_T, START_LAG_T). */
constexpr double kClocksPerT = 15.0 / 7.0;
constexpr double kFrameT     = 70003.0;    /* the machine's frame, in T-states */
constexpr double kFirstTickT = kFrameT - 120.0;

class TuneRun {
public:
    ms0515::Emulator emu;
    std::vector<Change> changes;

    /* Stage the folder device under %TEMP%/<name>, mount it, boot into the
     * program: RT-11's prompts answered, "R SABTUN" run by STARTS.COM. */
    explicit TuneRun(const char *name)
    {
        stage(name);
        REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
        emu.reset();
        REQUIRE(emu.mountDisk(0, (boot_ / "device.rtfs").string()));
        hookConsole();
        emu.setSoundCallback([this](int level) {
            changes.push_back({board().total_cycles, level, {}});
        });
    }

    ms0515_board_t &board() { return ms0515::internal::board(emu); }

    uint16_t sym(const char *name)
    {
        if (syms_.empty()) syms_ = symbols();
        auto it = syms_.find(name);
        REQUIRE_MESSAGE(it != syms_.end(), name);
        return it->second;
    }
    /* The program lives in the primary banks below 30000: RAM as addressed. */
    uint8_t  peek8(const char *name)  { return board().mem.ram[sym(name)]; }
    uint16_t peek16(const char *name)
    {
        const uint8_t *p = &board().mem.ram[sym(name)];
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    /* One video frame.  The frame interrupt is raised at its end and
     * taken at the next one's start: frameStarts holds those cycles. */
    void step()
    {
        if (frame_ < 900 && frame_ % 30 == 0) offerCR_ = true;   // the date prompts
        (void)emu.stepFrame();
        frameStarts.push_back(board().total_cycles);
        ++frame_;
    }
    std::vector<uint64_t> frameStarts;
    void settle(int n) { for (int i = 0; i < n; ++i) step(); }
    int frame() const { return frame_; }

    /* Run until the speaker has changed `count` times, or `maxFrames`
     * passed.  Returns the frames run. */
    int runUntilChanges(size_t count, int maxFrames)
    {
        int n = 0;
        while (changes.size() < count && n < maxFrames) { step(); ++n; }
        return n;
    }

    /* Run until the speaker has been quiet for `quietFrames` frames. */
    int runUntilQuiet(int quietFrames, int maxFrames)
    {
        int n = 0;
        size_t last = changes.size();
        int quiet = 0;
        while (n < maxFrames && quiet < quietFrames) {
            step(); ++n;
            if (changes.size() == last) ++quiet; else { quiet = 0; last = changes.size(); }
        }
        return n;
    }

    void keyTap(ms0515::Key k, int holdFrames = 6)
    {
        emu.keyPress(k, true);
        settle(holdFrames);
        emu.keyPress(k, false);
    }

private:
    fs::path boot_;
    bool offerCR_ = false;
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
        fs::copy_file(savPath(), boot_ / "SABTUN.SAV", fs::copy_options::overwrite_existing, ec);
        const char starts[] = "R SABTUN\r\n";
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

}  // namespace sab2tune
