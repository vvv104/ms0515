/*
 * test_game.cpp - the port booted through RT-11 on a folder device and
 * checked from outside: the title, ENTER, Willy walking and jumping, the
 * demo moving through the caverns, the 6031769 cheat and its teleport.
 * Every test skips when the game is not built.
 */
#include "ManicmGame.hpp"

using manicm::ManicmGame;
using ms0515::Key;

namespace {

/* Willy's cell in the attribute buffer: the column is its low five bits. */
int willyColumn(ManicmGame &g) { return g.peek16("WILLYA") & 31; }

}  // namespace

TEST_CASE("manicm: the title screen is up and ENTER starts Central Cavern")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_start");
    CHECK(g.vramNonzero() > 4000);                 // the title picture, not a blank
    g.startGame();
    CHECK(g.peek8("CAVNUM") == 0);
    CHECK(g.peek8("MODE") == 0);                   // the game, not the demo
    CHECK(g.peek8("LIVES") == 2);
    CHECK(g.peek16("WILLYA") == 0x4DA2);           // (13,2), as the cavern has him, less SHIFT
    CHECK(g.peek8("AIRBRN") == 0);
}

TEST_CASE("manicm: P walks Willy right, SPACE makes him jump")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_walk");
    g.startGame();
    const int x0 = willyColumn(g);
    g.emu.keyPress(Key::P, true);
    g.settle(60);                                  // a dozen passes
    g.emu.keyPress(Key::P, false);
    CHECK(willyColumn(g) > x0);
    g.settle(20);
    const int x1 = willyColumn(g);
    g.keyTap(Key::Space, 6);
    bool jumped = false;
    for (int i = 0; i < 20 && !jumped; ++i) { g.step(); jumped = g.peek8("AIRBRN") == 1; }
    CHECK(jumped);
    g.settle(60);
    CHECK(g.peek8("AIRBRN") == 0);                 // and landed
    CHECK(willyColumn(g) >= x1);
}

TEST_CASE("manicm: a key let go stops Willy within a pass or two")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_letgo");
    g.startGame();
    g.emu.keyPress(Key::P, true);
    g.settle(60);                                  // walking right, the key repeating
    g.emu.keyPress(Key::P, false);
    g.settle(12);                                  // two passes: the hold timers run out
    const int x = willyColumn(g);
    g.settle(40);
    CHECK(willyColumn(g) == x);                    // and he stood still from there
}

TEST_CASE("manicm: SPACE tapped while P is held jumps once, not again on landing")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_tap");
    g.startGame();
    g.emu.keyPress(Key::P, true);
    g.settle(30);                                  // walking right
    g.keyTap(Key::Space, 6);                       // a tap: P keeps repeating after it
    bool jumped = false;
    for (int i = 0; i < 20 && !jumped; ++i) { g.step(); jumped = g.peek8("AIRBRN") == 1; }
    REQUIRE(jumped);
    bool landed = false;
    for (int i = 0; i < 120 && !landed; ++i) { g.step(); landed = g.peek8("AIRBRN") == 0; }
    REQUIRE(landed);
    int jumpsAfter = 0;
    for (int i = 0; i < 60; ++i) { g.step(); if (g.peek8("AIRBRN") == 1) { ++jumpsAfter; break; } }
    g.emu.keyPress(Key::P, false);
    CHECK(jumpsAfter == 0);                        // SPACE was let go: no second jump
}

TEST_CASE("manicm: the demo runs through the caverns")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_demo");
    bool demo = false;
    for (int i = 0; i < 3000 && !demo; ++i) { g.step(); demo = g.peek8("MODE") != 0; }
    REQUIRE(demo);                                 // the tune, the message, then the demo
    CHECK(g.peek8("CAVNUM") == 0);
    bool next = false;
    for (int i = 0; i < 600 && !next; ++i) { g.step(); next = g.peek8("CAVNUM") >= 1; }
    CHECK(next);                                   // 64 passes a cavern, then the next
    CHECK(g.peek8("LIVES") == 2);                  // the demo costs no lives
}

TEST_CASE("manicm: 6031769, then 6 with a cavern number teleports")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_cheat");
    g.startGame();
    for (Key k : {Key::Digit6, Key::Digit0, Key::Digit3, Key::Digit1, Key::Digit7, Key::Digit6, Key::Digit9}) {
        g.keyTap(k, 6);
        g.settle(20);                              // the key let go before the next
    }
    CHECK(g.peek8("CHEATC") == 7);
    g.emu.keyPress(Key::Digit1, true);             // 1 + 2 + 4 = cavern 7, then 6 with them
    g.emu.keyPress(Key::Digit2, true);             //   (the codes come 2 ms apart down the
    g.emu.keyPress(Key::Digit3, true);             //   line: a pass must not fall between)
    g.settle(2);
    g.emu.keyPress(Key::Digit6, true);
    g.settle(12);
    g.emu.keyPress(Key::Digit6, false);
    g.emu.keyPress(Key::Digit1, false);
    g.emu.keyPress(Key::Digit2, false);
    g.emu.keyPress(Key::Digit3, false);
    g.settle(30);
    CHECK(g.peek8("CAVNUM") == 7);                 // Miner Willy meets the Kong Beast
}

TEST_CASE("manicm: P, then SPACE while P's repeat is still to come, jumps to the right")
{
    if (!manicm::built()) { MESSAGE("MANICM not built - skipped"); return; }
    ManicmGame g("manicm_diag");
    g.startGame();
    const int x0 = willyColumn(g);
    g.emu.keyPress(Key::P, true);
    g.settle(8);                                   // 160 ms: P has repeated once, SPACE cuts
    g.emu.keyPress(Key::Space, true);              //   the next repeat short - P must stay held
    bool jumped = false;
    for (int i = 0; i < 20 && !jumped; ++i) { g.step(); jumped = g.peek8("AIRBRN") == 1; }
    CHECK(jumped);
    g.emu.keyPress(Key::P, false);
    g.emu.keyPress(Key::Space, false);
    g.settle(120);                                 // a jump is 18 passes
    CHECK(g.peek8("AIRBRN") == 0);
    CHECK(willyColumn(g) > x0);                    // a jump to the right, not straight up
}
