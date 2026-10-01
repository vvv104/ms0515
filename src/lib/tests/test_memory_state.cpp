/*
 * test_memory_state.cpp - a machine kept entirely in memory: the diskette
 * mounted from a byte buffer and the state saved to and loaded from one.
 * Nothing here opens a file after the fixtures are read, which is what a
 * program with its ROM, diskette and state compiled in needs.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr const char *kRomB = ASSETS_DIR "/rom/ms0515-romb.rom";
constexpr const char *kSystemDisk = TESTS_DIR "/disks/originals/test_vvv_system.dsk";

std::vector<uint8_t> readAll(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void stepFrames(ms0515::Emulator &emu, int n)
{
    for (int i = 0; i < n; ++i)
        (void)emu.stepFrame();
}

void press(ms0515::Emulator &emu, ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, 2);
    emu.keyPress(key, false);
    stepFrames(emu, 4);
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

/* The monitor's dot on the last line in use, with the cursor or not. */
bool atPrompt(const std::vector<std::string> &rows)
{
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        if (!it->empty()) return *it == "." || *it == "._";
    return false;
}

bool shows(const std::vector<std::string> &rows, const std::string &text)
{
    for (const auto &r : rows)
        if (r.find(text) != std::string::npos) return true;
    return false;
}

}  /* namespace */

TEST_SUITE("memory state") {

TEST_CASE("a diskette mounted from memory boots, and writes stay in memory") {
    const auto rom = readAll(kRomB);
    const auto original = readAll(kSystemDisk);
    REQUIRE(original.size() == ms0515::kFloppyDiskSize);

    ms0515::Emulator emu;
    emu.loadRom(rom);
    CHECK(emu.diskImage(0).empty());
    REQUIRE(emu.mountDiskImage(0, original));
    CHECK(emu.diskPath(0).empty());
    REQUIRE(emu.diskImage(0).size() == ms0515::kFloppyDiskSize);
    emu.reset();
    stepFrames(emu, 1500);
    REQUIRE(atPrompt(screenRows(emu)));

    /* Whatever the system wrote went to the mounted copy alone. */
    CHECK(readAll(kSystemDisk) == original);

    emu.unmountDisk(0);
    CHECK(emu.diskImage(0).empty());

    /* Only a whole single-sided diskette is a diskette. */
    CHECK_FALSE(emu.mountDiskImage(0, std::vector<uint8_t>(1000)));
    CHECK_FALSE(emu.mountDiskImage(4, original));
    CHECK(emu.diskImage(0).empty());
}

TEST_CASE("a state saved to memory continues in another machine") {
    const auto rom = readAll(kRomB);

    ms0515::Emulator first;
    first.loadRom(rom);
    REQUIRE(first.mountDiskImage(0, readAll(kSystemDisk)));
    first.reset();
    stepFrames(first, 1500);
    REQUIRE(atPrompt(screenRows(first)));

    std::vector<uint8_t> state;
    REQUIRE(first.saveState(state));
    REQUIRE(state.size() > 128u * 1024);
    const std::vector<uint8_t> media(first.diskImage(0).begin(),
                                     first.diskImage(0).end());

    ms0515::Emulator second;
    second.loadRom(rom);
    REQUIRE(second.loadState(state));
    /* The state names no file for a diskette that had none: the drive
     * comes back empty and the media is put in again. */
    CHECK(second.diskImage(0).empty());
    REQUIRE(second.mountDiskImage(0, media));

    CHECK(second.pc() == first.pc());
    CHECK(second.sp() == first.sp());
    int differ = 0;
    for (uint32_t a = 0; a < 0160000; a += 2)
        if (second.readWord(static_cast<uint16_t>(a))
            != first.readWord(static_cast<uint16_t>(a))) ++differ;
    CHECK(differ == 0);
    CHECK(screenRows(second) == screenRows(first));

    /* The monitor goes on from there and reads the diskette: DIR. */
    press(second, ms0515::Key::D);
    press(second, ms0515::Key::I);
    press(second, ms0515::Key::R);
    press(second, ms0515::Key::Return);
    int guard = 0;
    while (!(shows(screenRows(second), ".SYS") && atPrompt(screenRows(second)))
           && guard++ < 600)
        stepFrames(second, 5);
    CHECK(shows(screenRows(second), ".SYS"));
    CHECK(atPrompt(screenRows(second)));
}

TEST_CASE("a broken state in memory is refused") {
    const auto rom = readAll(kRomB);
    ms0515::Emulator emu;
    emu.loadRom(rom);
    emu.reset();

    std::vector<uint8_t> state;
    REQUIRE(emu.saveState(state));

    CHECK_FALSE(emu.loadState(std::vector<uint8_t>{}));
    CHECK_FALSE(emu.loadState(std::vector<uint8_t>(64, 0x55)));
    std::vector<uint8_t> cut(state.begin(), state.begin() + state.size() / 2);
    CHECK_FALSE(emu.loadState(cut));

    /* Saved under another ROM. */
    ms0515::Emulator other;
    other.loadRom(readAll(ASSETS_DIR "/rom/ms0515-roma.rom"));
    CHECK_FALSE(other.loadState(state));

    CHECK(emu.loadState(state));
}

}
