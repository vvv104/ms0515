/*
 * test_turning.cpp - the turning diskette against the machine.
 *
 * The FPGA rebuild of the board (ms0515-fpga, docs/baseline.md, the open
 * list's item 9) ran Saboteur 2 from Omega's diskette: the game loads its
 * 85 blocks one request at a time and spends 23 ms between them copying
 * a block through the ROM, so the next block - two sectors on, its ID 21
 * ms after the last byte - has just gone by, and the controller waits a
 * turn: 240 ms a block, the cadence ending 24.8 s after the Return and
 * the title clearing at 27.1.  With the diskette turning the emulator
 * must take the same; without it the controller finds any sector in 5
 * ms and the game is up in a third of the time.
 */
#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>

#include <string>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

void tap(ms0515::Emulator &emu, ms0515::Key key, int frames = 4)
{
    emu.keyPress(key, true);
    for (int i = 0; i < frames; ++i) (void)emu.stepFrame();
    emu.keyPress(key, false);
    for (int i = 0; i < frames; ++i) (void)emu.stepFrame();
}

void run(ms0515::Emulator &emu, int frames)
{
    for (int i = 0; i < frames; ++i) (void)emu.stepFrame();
}

/* The machine's frames in a second, 50 at its rate. */
int fps(const ms0515::Emulator &emu)
{
    return static_cast<int>(1000.0 / emu.frameMs() + 0.5);
}

/* Boot omega-games (a real date at DATSET, Enter at the start file) and
 * type R SABOT2 up to its Return, as test_joystick does, with time to
 * spare for the turning diskette. */
void toSabot2(ms0515::Emulator &emu)
{
    using K = ms0515::Key;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    REQUIRE(emu.mountDisk(0, std::string{TESTS_DIR} + "/disks/originals/test_omega_games.dsk"));
    emu.reset();
    run(emu, 20 * fps(emu));
    for (K k : {K::Digit2, K::Digit2, K::MinusEq, K::Digit0, K::Digit8, K::MinusEq, K::Digit9, K::Digit2, K::Return})
        tap(emu, k);
    run(emu, 6 * fps(emu));
    tap(emu, K::Return);
    run(emu, 6 * fps(emu));
    for (K k : {K::R, K::Space, K::S, K::A, K::B, K::O, K::T, K::Digit2})
        tap(emu, k);
}

/* Seconds from now until the diskette has been quiet for three seconds,
 * less those three: when its last work was done.  -1 if it never is. */
double diskWorkSeconds(ms0515::Emulator &emu, int maxSeconds)
{
    const int still = 3 * fps(emu);
    int quiet = 0;
    for (int i = 0; i < maxSeconds * fps(emu); ++i) {
        (void)emu.stepFrame();
        quiet = emu.diskActive(0) ? 0 : quiet + 1;
        if (quiet == still) return static_cast<double>(i - still) / fps(emu);
    }
    return -1;
}

}  // namespace

TEST_CASE("the turning diskette: Saboteur 2 loads a block a turn, as on the machine")
{
    ms0515::Emulator emu;
    emu.setDisketteTurning(true);
    CHECK(emu.disketteTurning());
    toSabot2(emu);
    tap(emu, ms0515::Key::Return);
    const double seconds = diskWorkSeconds(emu, 60);
    REQUIRE(seconds >= 0);
    /* the board: the block-a-turn cadence ended 24.8 s after the Return,
     * the title cleared at 27.1; here the diskette's last work is at
     * 24.3 to 24.7 s */
    CHECK(seconds >= 23);
    CHECK(seconds <= 30);
}

TEST_CASE("the turning diskette: without it the same load takes a third of the time")
{
    ms0515::Emulator emu;
    CHECK(!emu.disketteTurning());
    toSabot2(emu);
    tap(emu, ms0515::Key::Return);
    const double seconds = diskWorkSeconds(emu, 60);
    REQUIRE(seconds >= 0);
    CHECK(seconds <= 12);
}
