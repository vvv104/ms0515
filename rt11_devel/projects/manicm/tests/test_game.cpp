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
    g.emu.keyPress(Key::Digit6, true);             // 6 with 1 + 2 + 4: cavern 7
    g.emu.keyPress(Key::Digit1, true);
    g.emu.keyPress(Key::Digit2, true);
    g.emu.keyPress(Key::Digit3, true);
    g.settle(12);
    g.emu.keyPress(Key::Digit6, false);
    g.emu.keyPress(Key::Digit1, false);
    g.emu.keyPress(Key::Digit2, false);
    g.emu.keyPress(Key::Digit3, false);
    g.settle(30);
    CHECK(g.peek8("CAVNUM") == 7);                 // Miner Willy meets the Kong Beast
}
