/*
 * test_clock_time.cpp - a monitor with timer support keeps the machine's
 * time: TIME set, a minute of frames run (the frame is the clock's tick,
 * 50 a second), TIME read back a minute later.  Counted in frames, so the
 * host's speed has no part in it.
 *
 * The vvv104 Omega (test_vvv_system.dsk, ROM-B) has SJ timer support.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>

#include "test_disk.hpp"

#include <filesystem>
#include <regex>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char *kRomB = ASSETS_DIR "/rom/ms0515-romb.rom";
constexpr const char *kDisk = TESTS_DIR "/disks/originals/test_vvv_system.dsk";
constexpr int kFramesPerSecond = 50;

void stepFrames(ms0515::Emulator &emu, ms0515::VramMirror &mirror, int n)
{
    for (int i = 0; i < n; ++i) {
        (void)emu.stepFrame();
        mirror.flushFrame();
    }
}

void tap(ms0515::Emulator &emu, ms0515::VramMirror &mirror, ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    stepFrames(emu, mirror, 8);
}

void typeString(ms0515::Emulator &emu, ms0515::VramMirror &mirror, const char *s)
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
    for (; *s; ++s) {
        const char c = *s;
        K k = K::None;
        if (c >= 'A' && c <= 'Z')      k = letters[c - 'A'];
        else if (c >= '0' && c <= '9') k = digits[c - '0'];
        else if (c == ' ')             k = K::Space;
        else if (c == ':')             k = K::ColonStar;
        else if (c == '\r')            k = K::Return;
        if (k != K::None) tap(emu, mirror, k);
    }
}

std::string screenAsText(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::string out;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        out += snap.row(r);
        out += '\n';
    }
    return out;
}

/* The seconds past 10:00:00 of the last time of day on the screen. */
int lastSecondsPastTen(const std::string &screen)
{
    static const std::regex rx{R"(10:(\d\d):(\d\d))"};
    int last = -1;
    for (auto it = std::sregex_iterator(screen.begin(), screen.end(), rx);
         it != std::sregex_iterator(); ++it)
        last = std::stoi((*it)[1]) * 60 + std::stoi((*it)[2]);
    return last;
}

}  /* namespace */

TEST_SUITE("clock") {

TEST_CASE("a minute of frames is a minute of TIME") {
    const std::string rom  = kRomB;
    const std::string disk = kDisk;
    REQUIRE(fs::exists(rom));
    REQUIRE(fs::exists(disk));

    ms0515_test::TempDisk td{disk};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(rom));
    REQUIRE(emu.mountDisk(0, td.path().string()));
    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(nullptr);
    emu.reset();

    stepFrames(emu, mirror, 30 * kFramesPerSecond);      /* boot, STARTS.COM */
    typeString(emu, mirror, "TIME 10:00:00\r");
    stepFrames(emu, mirror, 60 * kFramesPerSecond);
    typeString(emu, mirror, "TIME\r");
    stepFrames(emu, mirror, 2 * kFramesPerSecond);

    const std::string screen = screenAsText(emu);
    INFO("screen:\n" << screen);
    /* Set when its Return went in; read when the second TIME's Return
     * did: 60 s of frames and the ten taps of "TIME\r" between. */
    const int expected = 60 + (5 * 10) / kFramesPerSecond;
    const int seen = lastSecondsPastTen(screen);
    CHECK(seen >= expected - 1);
    CHECK(seen <= expected + 1);
}

}
