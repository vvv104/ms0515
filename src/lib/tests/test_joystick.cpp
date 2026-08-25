/*
 * test_joystick.cpp - the joystick as SABOT2 sees it.
 *
 * The 1991 SABOT2 on omega-games.dsk reads the MS7007 port B (0177542) in
 * its "J KEMPSTON" mode - MOV @#177542,R0 / COM R0 / BIC #177740,R0 - and
 * keeps the lines held in a byte at 041460.  Boot the disk, start the game
 * with the joystick option, and that byte must follow the lines we hold:
 * the OS-and-game oracle for the port, not a self-consistency check.
 */
#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>

#include <string>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

constexpr uint16_t kJoyByte = 041460;   /* the game's joystick byte (1991 build) */

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

/* Boot omega-games (a real date at DATSET, Enter at the start file), R
 * SABOT2, J (Kempston), S (start), through the briefing into the game. */
void startSabot2WithJoystick(ms0515::Emulator &emu)
{
    using K = ms0515::Key;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    REQUIRE(emu.mountDisk(0, std::string{ASSETS_DIR} + "/disks/omega-games.dsk"));
    emu.reset();
    run(emu, 400);
    for (K k : {K::Digit2, K::Digit2, K::MinusEq, K::Digit0, K::Digit8, K::MinusEq, K::Digit9, K::Digit2, K::Return})
        tap(emu, k);
    run(emu, 150);
    tap(emu, K::Return);
    run(emu, 150);
    for (K k : {K::R, K::Space, K::S, K::A, K::B, K::O, K::T, K::Digit2, K::Return})
        tap(emu, k);
    run(emu, 600);                       /* the game loads: the title menu */
    tap(emu, K::J, 3);
    tap(emu, K::S, 3);
    run(emu, 600);                       /* the mission briefing */
    tap(emu, K::Kp5, 3);                 /* any key: into the game */
    run(emu, 100);
}

}  // namespace

/* Hold `bits` and wait for the game's byte to show them (it polls the port
 * every few frames, and holds fire against the hang-glider change what it
 * does next); returns the frames it took, or -1. */
int settle(ms0515::Emulator &emu, uint8_t bits, int maxFrames = 120)
{
    emu.setJoystick(bits);
    for (int i = 0; i < maxFrames; ++i) {
        (void)emu.stepFrame();
        if (emu.readByte(kJoyByte) == bits) return i;
    }
    return -1;
}

TEST_CASE("joystick: SABOT2 (1991) sees the lines held on the MS7007 port")
{
    using Joy = ms0515::Emulator::Joy;
    ms0515::Emulator emu;
    startSabot2WithJoystick(emu);

    CHECK(settle(emu, 0) >= 0);                        /* open lines: nothing held */
    CHECK(settle(emu, Joy::Right) >= 0);
    CHECK(settle(emu, Joy::Left | Joy::Down) >= 0);
    CHECK(settle(emu, Joy::Up) >= 0);
    CHECK(settle(emu, 0) >= 0);
    CHECK(settle(emu, Joy::Up | Joy::Fire) >= 0);
    CHECK(emu.joystick() == (Joy::Up | Joy::Fire));
    emu.setJoystick(0);
    CHECK(emu.joystick() == 0);
}
