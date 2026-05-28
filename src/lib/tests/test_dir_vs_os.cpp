/*
 * test_dir_vs_os.cpp — cross-check ms0515_disk's directory parse against
 * the real OS.
 *
 * Boots a reference disk, runs the OS's own `DIR` command, captures the
 * decoded terminal text (Terminal::decode — the same text the CLI would
 * print, not raw VRAM), and compares the file list + block sizes to what
 * the ms0515::disk library parses from the same image.  Agreement proves
 * our directory layout/parse matches the OS's own view.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/disk/Image.hpp>

#include "test_disk.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif
#ifndef TESTS_DIR
#error "TESTS_DIR must be defined by the build system"
#endif

namespace {

constexpr const char *kRomA = ASSETS_DIR "/rom/ms0515-roma.rom";
constexpr const char *kRomB = ASSETS_DIR "/rom/ms0515-romb.rom";

void stepFrames(ms0515::Emulator &emu, ms0515::VramMirror &mirror, int n)
{
    for (int i = 0; i < n; ++i) { (void)emu.stepFrame(); mirror.flushFrame(); }
}

void tap(ms0515::Emulator &emu, ms0515::VramMirror &mirror, ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    stepFrames(emu, mirror, 8);
}

void waitForIdle(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                 int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap; ++i) {
        const size_t before = mirror.history().size();
        (void)emu.stepFrame(); mirror.flushFrame();
        if (mirror.history().size() == before) ++q; else q = 0;
        if (q >= quiet) return;
    }
}

ms0515::Key letterKey(char c)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G, K::H, K::I, K::J, K::K,
        K::L, K::M, K::N, K::O, K::P, K::Q, K::R, K::S, K::T, K::U, K::V,
        K::W, K::X, K::Y, K::Z,
    };
    return letters[c - 'A'];
}

void typeLine(ms0515::Emulator &emu, ms0515::VramMirror &mirror, const char *s)
{
    using K = ms0515::Key;
    static constexpr K digits[10] = {
        K::Digit0, K::Digit1, K::Digit2, K::Digit3, K::Digit4,
        K::Digit5, K::Digit6, K::Digit7, K::Digit8, K::Digit9,
    };
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') tap(emu, mirror, letterKey(c));
        else if (c >= '0' && c <= '9') tap(emu, mirror, digits[c - '0']);
        else if (c == ' ')        tap(emu, mirror, K::Space);
        else if (c == ':')        tap(emu, mirror, K::ColonStar);
        else if (c == '.')        tap(emu, mirror, K::Period);
    }
    tap(emu, mirror, K::Return);
}

std::vector<std::string> screenRows(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        std::string row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        rows.push_back(row);
    }
    return rows;
}

/* Pull (FILENAME.EXT -> blocks) pairs out of RT-11 DIR text.  Names are
 * printed in a 6.3 field (internal spaces for short names) followed by
 * the block count. */
std::map<std::string, int> parseOsDir(const std::vector<std::string> &rows)
{
    std::map<std::string, int> out;
    const std::regex re(R"(([A-Z0-9$]{1,6}) *\.([A-Z0-9$ ]{1,3}) +([0-9]+))");
    for (const auto &row : rows) {
        for (std::sregex_iterator it(row.begin(), row.end(), re), end;
             it != end; ++it) {
            std::string name = (*it)[1].str();
            std::string ext  = (*it)[2].str();
            ext.erase(ext.find_last_not_of(' ') + 1);
            const int blocks = std::stoi((*it)[3].str());
            out[ext.empty() ? name : name + "." + ext] = blocks;
        }
    }
    return out;
}

std::map<std::string, int> parseToolDir(const std::string &imagePath)
{
    std::map<std::string, int> out;
    auto img = ms0515::disk::loadImage(imagePath);
    if (img && img->hasDirectory)
        for (const auto &e : img->directory.permanentFiles())
            out[e.name] = e.length;
    return out;
}

void dumpRows(const char *label, const std::vector<std::string> &rows)
{
    std::fprintf(stderr, "--- %s ---\n", label);
    for (size_t r = 0; r < rows.size(); ++r)
        if (!rows[r].empty())
            std::fprintf(stderr, "  %2zu| %s\n", r, rows[r].c_str());
    std::fprintf(stderr, "----------------\n");
}

struct DiskConfig {
    const char *disk;   /* file under TESTS_DIR/disks/ */
    const char *rom;    /* ROM image */
    const char *name;   /* subcase label */
};

constexpr DiskConfig kConfigs[] = {
    {"test_osa.dsk",   kRomA, "OSA"},
    {"test_omega.dsk", kRomB, "Omega"},
    {"test_mihin.dsk", kRomB, "Mihin"},
    {"test_rod.dsk",   kRomA, "RT-15SJ"},
};

/* Boot the disk, run DIR, return (OS-parsed, tool-parsed) file maps. */
void runDirCheck(const DiskConfig &cfg,
                 std::map<std::string, int> &osDir,
                 std::map<std::string, int> &toolDir)
{
    const std::string disk = std::string(TESTS_DIR) + "/disks/" + cfg.disk;
    ms0515_test::TempDisk td{disk};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(cfg.rom));
    REQUIRE(emu.mountDisk(0, td.path().string()));
    /* Double-sided fixtures need the upper-side unit mounted too. */
    std::error_code ec;
    if (std::filesystem::file_size(td.path(), ec) == 2u * 409600u)
        REQUIRE(emu.mountDisk(2, td.path().string()));

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(nullptr);
    emu.reset();

    waitForIdle(emu, mirror, /*quiet=*/120, /*cap=*/3500);
    typeLine(emu, mirror, "DIR");
    waitForIdle(emu, mirror, /*quiet=*/120, /*cap=*/4000);

    auto rows = screenRows(emu);
    osDir   = parseOsDir(rows);
    toolDir = parseToolDir(td.path().string());
    if (osDir != toolDir) dumpRows(cfg.name, rows);
}

}  /* namespace */

TEST_SUITE("DirVsOs") {

TEST_CASE("OS DIR matches ms0515_disk parse across reference disks") {
    for (const auto &cfg : kConfigs) {
        SUBCASE(cfg.name) {
            const std::string label = cfg.name;
            std::map<std::string, int> osDir, toolDir;
            runDirCheck(cfg, osDir, toolDir);

            CHECK_MESSAGE(!toolDir.empty(), label << ": tool found no files");
            CHECK_MESSAGE(!osDir.empty(),   label << ": OS DIR produced nothing");
            CHECK_MESSAGE(osDir == toolDir,
                          label << ": OS DIR vs tool file-list/size mismatch");
        }
    }
}

} /* TEST_SUITE */
