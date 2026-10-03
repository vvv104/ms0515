/*
 * SlMachine.hpp - the machine SL's tests run on.
 *
 * The dec system diskette the build composed from the software collection
 * (CMakeLists.txt), with the SL under test put on it and a startup file of
 * the test's choosing, booted headlessly on ROM-B to the monitor's prompt.
 * The test types at it as a user would - a host character becomes the
 * MS 7004 key that sends it - and reads back the screen.  The SL is the
 * collection's (kits/dec/handlers/SL.SYS) unless --sl-sys=<file> names
 * another, a fresh build; the suite skips itself when there is none.
 */
#pragma once

#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Layout.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sl {

namespace fs = std::filesystem;

/* Harness options: --sl-<name>=<value> on the command line. */
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

inline std::string slPath()   { return optOr("sys",  SL_SYS); }
inline std::string diskPath() { return optOr("disk", SL_SYSTEM_DISK); }
inline std::string romPath()  { return optOr("rom",  std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"); }

/* There is an SL to test?  Tests skip otherwise. */
inline bool built() { return fs::exists(slPath()) && fs::exists(diskPath()); }

inline std::vector<uint8_t> readAll(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/* The startups the tests boot with, and the file the monitor reads them
 * from. */
inline constexpr const char *kStartupFile = "STARTS.COM";
inline constexpr const char *kQuiet = "SET TT QUIET\r\n";
inline constexpr const char *kSlOn  = "SET TT QUIET\r\nSET SL ON\r\n";

/* A host character as the key that sends it (Latin mode: a bare letter is
 * upper case, as the monitor has it). */
struct KeyStroke { ms0515::Key key; bool shift; };
inline KeyStroke strokeFor(char c);

class SlMachine {
public:
    ms0515::Emulator emu;

    /* Boot the diskette with SL and `startup` as its startup file, up to
     * an idle prompt. */
    explicit SlMachine(const char *startup = kSlOn)
    {
        stage(startup);
        REQUIRE(emu.loadRomFile(romPath()));
        REQUIRE(emu.mountDisk(0, image_.string()));
        emu.reset();
        waitPrompt(8000);
        REQUIRE_MESSAGE(atPrompt(), "no prompt after boot:\n" << screen());
    }
    ~SlMachine()
    {
        emu.unmountDisk(0);
        std::error_code ec;
        fs::remove(image_, ec);
    }
    SlMachine(const SlMachine &)            = delete;
    SlMachine &operator=(const SlMachine &) = delete;

    /* Frames until the screen has not changed for `quiet` frames. */
    void settle(int cap = 300, int quiet = 20)
    {
        std::string last = screen();
        int still = 0;
        for (int i = 0; i < cap && still < quiet; ++i) {
            (void)emu.stepFrame();
            if (i % 4) continue;
            std::string now = screen();
            still = now == last ? still + 4 : 0;
            last = std::move(now);
        }
    }

    /* Frames until the monitor's bare prompt has stood for `quiet`
     * frames: a command may read the floppy for a long, silent while. */
    void waitPrompt(int cap = 6000, int quiet = 60)
    {
        int still = 0;
        for (int i = 0; i < cap && still < quiet; i += 4) {
            for (int j = 0; j < 4; ++j) (void)emu.stepFrame();
            still = atPrompt() ? still + 4 : 0;
        }
    }

    /* Frames until the line being edited shows as `text`: a program is
     * read off the floppy with the screen standing still. */
    void waitShown(const std::string &text, int cap = 6000)
    {
        for (int i = 0; i < cap && shown() != text; i += 4)
            for (int j = 0; j < 4; ++j) (void)emu.stepFrame();
        settle();
    }

    void tap(ms0515::Key k, bool shift = false, bool ctrl = false)
    {
        if (shift) emu.keyPress(ms0515::Key::ShiftL, true);
        if (ctrl)  emu.keyPress(ms0515::Key::Ctrl, true);
        emu.keyPress(k, true);
        (void)emu.stepFrame();
        emu.keyPress(k, false);
        if (ctrl)  emu.keyPress(ms0515::Key::Ctrl, false);
        if (shift) emu.keyPress(ms0515::Key::ShiftL, false);
        for (int i = 0; i < 4; ++i) (void)emu.stepFrame();
    }
    /* Type host text; '\r' is Return.  Text ending in Return waits for
     * the prompt to come back, other text for the screen to settle. */
    void type(const std::string &text)
    {
        for (char c : text) {
            const auto s = strokeFor(c);
            REQUIRE_MESSAGE(s.key != ms0515::Key::None, "no key for " << int(c));
            tap(s.key, s.shift);
        }
        if (!text.empty() && text.back() == '\r') waitPrompt();
        else settle();
    }
    /* Ctrl + a letter. */
    void ctrl(char letter) { tap(strokeFor(letter).key, false, true); settle(); }
    void key(ms0515::Key k) { tap(k); settle(); }
    /* PF1 - SL's GOLD - and then the key it shifts. */
    void gold(ms0515::Key k) { key(ms0515::Key::Pf1); key(k); }

    std::string row(int r)
    {
        auto s = ms0515::Terminal{}.decode(emu).row(r);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
    std::string screen()
    {
        std::string out;
        for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
            out += row(r);
            out += '\n';
        }
        return out;
    }
    /* The last non-blank row, and whether it is the bare prompt. */
    std::string lastRow()
    {
        for (int r = ms0515::Terminal::kRows - 1; r >= 0; --r)
            if (auto s = row(r); !s.empty()) return s;
        return {};
    }
    bool atPrompt() { const auto s = lastRow(); return s == "." || s == "._"; }
    /* The line being edited as the screen shows it: the last row, the
     * ROM's cursor (an underline past the end) dropped. */
    std::string shown()
    {
        auto s = lastRow();
        while (!s.empty() && (s.back() == '_' || s.back() == ' ')) s.pop_back();
        return s;
    }
    /* How many rows of the screen are exactly `text`. */
    int rowsEqual(const std::string &text)
    {
        int n = 0;
        for (int r = 0; r < ms0515::Terminal::kRows; ++r) n += row(r) == text;
        return n;
    }

private:
    fs::path image_;

    void stage(const char *startup)
    {
        namespace d = ms0515::disk;
        auto image = readAll(diskPath());
        REQUIRE(image.size() == d::kSideSize);
        const auto sys = readAll(slPath());
        try { d::removeFile(image, 0, false, "SL.SYS"); } catch (const std::exception &) {}
        d::putFile(image, 0, false, "SL.SYS", sys);
        try { d::removeFile(image, 0, false, kStartupFile); } catch (const std::exception &) {}
        const std::string text{startup};
        d::putFile(image, 0, false, kStartupFile,
                   std::span{reinterpret_cast<const uint8_t *>(text.data()), text.size()});
        image_ = fs::temp_directory_path() / ("ms0515_sl_" + std::to_string(std::random_device{}()) + ".dsk");
        std::ofstream o(image_, std::ios::binary);
        o.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
};

inline KeyStroke strokeFor(char c)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G, K::H, K::I, K::J, K::K, K::L, K::M,
        K::N, K::O, K::P, K::Q, K::R, K::S, K::T, K::U, K::V, K::W, K::X, K::Y, K::Z,
    };
    static constexpr K digits[10] = {
        K::Digit0, K::Digit1, K::Digit2, K::Digit3, K::Digit4,
        K::Digit5, K::Digit6, K::Digit7, K::Digit8, K::Digit9,
    };
    if (c >= 'A' && c <= 'Z') return {letters[c - 'A'], false};
    if (c >= 'a' && c <= 'z') return {letters[c - 'a'], true};
    if (c >= '0' && c <= '9') return {digits[c - '0'], false};
    switch (c) {
    case ' ':  return {K::Space, false};
    case '\r': return {K::Return, false};
    case '.':  return {K::Period, false};
    case ',':  return {K::Comma, false};
    case '/':  return {K::Slash, false};
    case ':':  return {K::ColonStar, false};
    case '*':  return {K::ColonStar, true};
    case '-':  return {K::MinusEq, false};
    case '=':  return {K::MinusEq, true};
    default:   return {K::None, false};
    }
}

}  // namespace sl
