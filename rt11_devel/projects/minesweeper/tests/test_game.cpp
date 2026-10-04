/*
 * test_game.cpp - the minesweeper played.
 *
 * The field the game lays is read from its memory, so that a test knows
 * which cell to open; what the player sees is read from the screen.
 */
#include "MineGame.hpp"

#include <iostream>

using K = ms0515::Key;

namespace {

/* The first closed cell of the field holding `what` (0..8, or 9 a mine). */
bool find(mine::MineGame &g, int what, int &x, int &y)
{
    for (y = 1; y <= g.height(); ++y)
        for (x = 1; x <= g.width(); ++x)
            if (g.field(x, y) == what) return true;
    return false;
}

int count(mine::MineGame &g, int what)
{
    int n = 0;
    for (int y = 1; y <= g.height(); ++y)
        for (int x = 1; x <= g.width(); ++x) n += g.field(x, y) == what;
    return n;
}

}  // namespace

TEST_SUITE("minesweeper") {

TEST_CASE("the game opens on the Beginner field: 8 by 8, ten mines, all closed") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    REQUIRE_FALSE(g.ended());
    CHECK(g.width() == 8);
    CHECK(g.height() == 8);
    CHECK(g.mines() == 10);
    CHECK(count(g, 9) == 10);
    CHECK(g.cursorX() == 1);
    CHECK(g.cursorY() == 1);
    for (int y = 1; y <= 8; ++y)
        for (int x = 1; x <= 8; ++x)
            if (x != 1 || y != 1) CHECK_MESSAGE(g.cell(x, y) == mine::kClosed, "cell " << x << "," << y);
}

TEST_CASE("the sum the game counts over its own file is the one stored after it") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    const auto sav = mine::readAll(g.dir() / "K.SAV");
    const size_t at = sav.size() - 1024;
    const uint16_t stored = static_cast<uint16_t>(sav[at] | sav[at + 1] << 8);
    CHECK(g.word(01000) == stored);             /* CHECK leaves its sum at 1000 */
}

TEST_CASE("Space opens a cell: a number shows the mines round it") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    int x = 0, y = 0;
    int n = 1;
    while (n <= 8 && !find(g, n, x, y)) ++n;
    REQUIRE(n <= 8);
    g.moveTo(x, y);
    g.space();
    CHECK(g.field(x, y) == n + 10);
    g.moveTo(x == 1 ? 2 : 1, y);                /* the cursor off the cell */
    CHECK(g.cell(x, y) == n);
    CHECK_FALSE(g.ended());
}

TEST_CASE("an empty cell opens its neighbours") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    int x = 0, y = 0;
    if (!find(g, 0, x, y)) { MESSAGE("no empty cell on this field"); return; }
    g.moveTo(x, y);
    g.space();
    CHECK(g.field(x, y) == 10);
    CHECK(count(g, 10) >= 1);
    int open = 0;
    for (int j = 1; j <= 8; ++j)
        for (int i = 1; i <= 8; ++i) open += g.field(i, j) >= 10 && g.field(i, j) <= 18;
    CHECK(open >= 4);                           /* the cell and what lies round it */
}

TEST_CASE("Return marks a mine and takes the mark off") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    g.moveTo(3, 3);
    const int before = g.field(3, 3);
    g.enter();
    CHECK(g.field(3, 3) == (before == 9 ? 19 : before + 20));
    g.moveTo(4, 3);
    CHECK(g.cell(3, 3) == mine::kFlag);
    g.moveTo(3, 3);
    g.enter();
    CHECK(g.field(3, 3) == before);
    g.moveTo(4, 3);
    CHECK(g.cell(3, 3) == mine::kClosed);
}

TEST_CASE("a mine opened ends the game: the mines shown, this one blown") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    int x = 0, y = 0;
    REQUIRE(find(g, 9, x, y));
    g.moveTo(x, y);
    g.space();
    CHECK(g.cell(x, y) == mine::kBlown);
    int shown = 0;
    for (int j = 1; j <= 8; ++j)
        for (int i = 1; i <= 8; ++i) shown += g.cell(i, j) == mine::kMine;
    CHECK(shown == 9);
    CHECK_FALSE(g.ended());                     /* it waits for a key */
    g.space();
    g.idle(3000);
    CHECK(g.ended());
}

TEST_CASE("every cell without a mine opened is the game won") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    for (int y = 1; y <= 8 && !g.ended(); ++y)
        for (int x = 1; x <= 8 && !g.ended(); ++x)
            if (g.field(x, y) <= 8) {
                g.moveTo(x, y);
                g.space();
            }
    int flags = 0;
    for (int j = 1; j <= 8; ++j)
        for (int i = 1; i <= 8; ++i) flags += g.cell(i, j) == mine::kFlag;
    CHECK(flags == 10);                         /* the mines, flagged for the winner */
    g.space();
    g.idle(3000);
    CHECK(g.ended());
}

TEST_CASE("F4 and F5 lay the larger fields, F3 the first again") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    g.press(K::F4);
    g.idle(6000);
    CHECK(g.width() == 16);
    CHECK(g.height() == 16);
    CHECK(count(g, 9) == 40);
    CHECK(g.cell(16, 16) == mine::kClosed);
    g.press(K::F5);
    g.idle(9000);
    CHECK(g.width() == 30);
    CHECK(count(g, 9) == 99);
    CHECK(g.cell(30, 16) == mine::kClosed);
    g.press(K::F3);
    g.idle(6000);
    CHECK(g.width() == 8);
    CHECK(count(g, 9) == 10);
}

TEST_CASE("F9 is the menu, and its items work") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    g.press(K::F9);
    const auto menu = g.screen();
    INFO("menu:\n" << menu);
    CHECK(menu.find("Help         F1") != std::string::npos);
    CHECK(menu.find("Intermediate F4") != std::string::npos);
    CHECK(menu.find("Marker(?) off") != std::string::npos);
    CHECK(menu.find("Version") != std::string::npos);
    /* Down four times: Intermediate; Return takes it. */
    for (int i = 0; i < 4; ++i) g.press(K::Down);
    g.enter();
    g.idle(6000);
    CHECK(g.width() == 16);
}

TEST_CASE("F1 is the help: the text deciphered, two screens") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    g.press(K::F1);
    g.idle(9000);
    const auto first = g.screen();
    INFO("help:\n" << first);
    CHECK(first.find("Voronkov Soft") != std::string::npos);
    CHECK(first.find("Minesweeper") != std::string::npos);
    g.press(K::Down);
    g.idle(3000);
    const auto second = g.screen();
    INFO("second screen:\n" << second);
    CHECK(second.find("Beginner") != std::string::npos);
    CHECK(second != first);
    g.space();                                  /* any other key: back to the game */
    g.idle(6000);
    CHECK(g.cell(8, 8) == mine::kClosed);
}

TEST_CASE("F10 leaves the game") {
    if (!mine::built()) { MESSAGE("the game is not built - skipped"); return; }
    mine::MineGame g;
    g.press(K::F10);
    g.idle(3000);
    CHECK(g.ended());
}

}
