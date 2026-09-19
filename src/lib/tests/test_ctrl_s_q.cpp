/*
 * test_ctrl_s_q.cpp - ^S holds a monitor's output and one ^Q lets it go on,
 * whatever the moment ^Q comes: a TYPE of 300 lines held by ^S, the machine
 * saved, then ^Q pressed at each of 30 frames in turn from the same state,
 * and the text must run to its end every time.  Keys and frames go into the
 * Emulator as the GUI puts them, so nothing of the CLI's bridge takes part.
 *
 * It pins the order of the board's interrupt encoder (core/src/cpu.c): with
 * the monitor's request below the keyboard's, as the NS4 schematic has it,
 * OSA's ^Q sticks at every one of these moments.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/disk/Build.hpp>

#include "test_disk.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace disk = ms0515::disk;

namespace {

constexpr const char *kRomA = ASSETS_DIR "/rom/ms0515-roma.rom";
constexpr const char *kRomB = ASSETS_DIR "/rom/ms0515-romb.rom";
constexpr const char *kOriginals = TESTS_DIR "/disks/originals/";
constexpr int kLines = 300;

struct System { const char *name; const char *rom; const char *disk; };
const System kSystems[] = {
    {"Omega (vvv104), ROM-B", kRomB, "test_vvv_system.dsk"},
    {"OSA, ROM-A",            kRomA, "test_osa_games.dsk"},
};

std::string longText()
{
    std::string s;
    char line[80];
    for (int i = 0; i < kLines; ++i) {
        std::snprintf(line, sizeof line,
                      "LINE %04d THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG\r\n", i);
        s += line;
    }
    return s;
}

void stepFrames(ms0515::Emulator &emu, ms0515::VramMirror &mirror, int n)
{
    for (int i = 0; i < n; ++i) {
        (void)emu.stepFrame();
        mirror.flushFrame();
    }
}

void press(ms0515::Emulator &emu, ms0515::VramMirror &mirror, ms0515::Key key,
           bool ctrl = false)
{
    if (ctrl) emu.keyPress(ms0515::Key::Ctrl, true);
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    if (ctrl) emu.keyPress(ms0515::Key::Ctrl, false);
    stepFrames(emu, mirror, 4);
}

void typeLine(ms0515::Emulator &emu, ms0515::VramMirror &mirror, const std::string &s)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G, K::H, K::I, K::J, K::K, K::L, K::M,
        K::N, K::O, K::P, K::Q, K::R, K::S, K::T, K::U, K::V, K::W, K::X, K::Y, K::Z,
    };
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') press(emu, mirror, letters[c - 'A']);
        else if (c == ' ')        press(emu, mirror, K::Space);
        else if (c == '.')        press(emu, mirror, K::Period);
    }
    press(emu, mirror, K::Return);
}

std::vector<std::string> screenRows(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        auto row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        rows.push_back(row);
    }
    return rows;
}

bool showsLine(const std::vector<std::string> &rows, int n)
{
    char tag[16];
    std::snprintf(tag, sizeof tag, "LINE %04d", n);
    for (const auto &r : rows)
        if (r.find(tag) != std::string::npos) return true;
    return false;
}

/* The text ran to its end: its last line on the screen and the prompt
 * after it. */
bool finished(const std::vector<std::string> &rows)
{
    int last = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (!rows[i].empty()) last = i;
    /* the prompt, with the cursor after it or not */
    return last > 0 && (rows[last] == "." || rows[last] == "._")
        && showsLine(rows, kLines - 1);
}

}  /* namespace */

TEST_SUITE("ctrl-s ctrl-q") {

TEST_CASE("^Q resumes a TYPE held by ^S, whenever it comes") {
    for (const System &sys : kSystems) {
        const std::string name{sys.name};
        CAPTURE(name);
        const std::string src = std::string{kOriginals} + sys.disk;
        REQUIRE(fs::exists(src));
        ms0515_test::TempDisk td{src};
        {
            std::ifstream in(td.path(), std::ios::binary);
            std::vector<uint8_t> img{std::istreambuf_iterator<char>(in), {}};
            in.close();
            const std::string text = longText();
            disk::putFile(img, 0, img.size() > 409600, "LONG.TXT",
                          {reinterpret_cast<const uint8_t *>(text.data()), text.size()});
            std::ofstream out(td.path(), std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(img.data()),
                      static_cast<std::streamsize>(img.size()));
        }
        ms0515::Emulator emu;
        REQUIRE(emu.loadRomFile(sys.rom));
        REQUIRE(emu.mountDisk(0, td.path().string()));
        ms0515::VramMirror mirror;
        mirror.attach(emu);
        mirror.setOutput(nullptr);
        emu.reset();
        stepFrames(emu, mirror, 1500);                  /* boot, STARTS.COM */

        typeLine(emu, mirror, "TYPE LONG.TXT");
        int guard = 0;
        while (!showsLine(screenRows(emu), 5) && guard++ < 1000)
            stepFrames(emu, mirror, 1);
        REQUIRE(showsLine(screenRows(emu), 5));
        press(emu, mirror, ms0515::Key::S, true);
        stepFrames(emu, mirror, 100);
        const auto held = screenRows(emu);
        stepFrames(emu, mirror, 100);
        REQUIRE(screenRows(emu) == held);               /* ^S holds it */
        REQUIRE_FALSE(showsLine(held, kLines - 1));

        const fs::path state = td.path().string() + ".held";
        REQUIRE(emu.saveState(state.string()));
        int stuck = 0;
        for (int offset = 0; offset < 30; ++offset) {
            REQUIRE(emu.loadState(state.string()));
            stepFrames(emu, mirror, offset);
            press(emu, mirror, ms0515::Key::Q, true);
            int f = 0;
            while (!finished(screenRows(emu)) && f < 3000) {
                stepFrames(emu, mirror, 10);
                f += 10;
            }
            if (!finished(screenRows(emu))) {
                ++stuck;
                MESSAGE(name << ": ^Q " << offset << " frames after the hold - stuck");
                if (stuck == 1) {
                    std::string scr;
                    for (const auto &r : screenRows(emu)) scr += r + "|\n";
                    MESSAGE(scr);
                }
            }
        }
        fs::remove(state);
        CHECK(stuck == 0);
    }
}

}
