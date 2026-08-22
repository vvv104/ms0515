/*
 * test_game.cpp - the standalone game, booted through RT-11 on a folder
 * device and checked from the outside: the render, the HUD, the start-up
 * sequence, the keyboard map, the match logic, the dojo change.  Every test
 * skips when the game is not built.
 */
#include "FistGame.hpp"

#include <algorithm>
#include <utility>

using fist::FistGame;

namespace {

/* Static picture rows 24..63 (below the row-6 strip, above any fighter),
 * picture columns 4..35 -> bytes [row*80+8, row*80+72): compared byte for
 * byte with the Python reference render of background `ref`. */
int bgMismatches(FistGame &g, const std::vector<uint8_t> &expect)
{
    const uint8_t *v = g.vram();
    int bad = 0;
    for (int row = 24; row < 64; ++row)
        for (int b = row * 80 + 8; b < row * 80 + 72; ++b)
            if (v[b] != expect[b]) ++bad;
    return bad;
}

/* Force P1 to score a clean full point (2 -> 4 = two yin-yang) until `until`. */
template <class F>
bool p1Wins(FistGame &g, int maxFrames, F until)
{
    for (int i = 0; i < maxFrames; ++i) {
        if (g.gst(0xAA01) < 4) {
            g.poke(0xAA01, 2); g.poke(0xAA08, 2);        // P1 at 2, scored a full point
            g.poke(0xAA03, 0x10); g.poke(0xAA43, 0);     // a clean hit (SCDET bit 4)
            g.poke(0x9C28, 1);                           // knocked into recovery
        }
        g.step();
        if (until()) return true;
    }
    return false;
}

/* The numeric score strip (row 6, bytes 34..47): a hash + pixel sum. */
std::pair<unsigned, unsigned> scoreSig(FistGame &g)
{
    const uint8_t *v = g.vram();
    unsigned h = 0, n = 0;
    for (int r = 6; r < 14; ++r)
        for (int c = 34; c < 48; ++c) { h = h * 131 + v[r * 80 + c]; n += v[r * 80 + c]; }
    return {h, n};
}

}  // namespace

TEST_CASE("fist: standalone game renders through the .DAT loader")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    FistGame g("fist_game_lib", 3000);
    std::string out = fist::opt("vram-out");
    if (!out.empty()) g.dumpVram(out);
    MESSAGE("final CPU PC = " << std::oct << cpu_get_pc(&g.cpu()) << std::dec
            << "  VRAM non-zero bytes: " << g.vramNonzero());
    // A live two-fighter frame over the dojo is thousands of non-zero bytes;
    // a blank / trapped screen is a few hundred.
    CHECK(g.vramNonzero() > 3000);
}

TEST_CASE("fist: yin-yang HUD at a forced score")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    FistGame g("fist_forced_lib");
    // Idle fighters rarely trigger ROUNDE, so a forced total holds:
    // P1 = 3 (full inner + half outer), P2 = 2 (full inner).
    for (int i = 0; i < 300; ++i) {
        g.poke(0xAA01, 3); g.poke(0xAA41, 2);
        g.step();
    }
    std::string out = fist::opt("vram-out");
    if (!out.empty()) g.dumpVram(out);
    // The four yin-yang slots sit in rows 6-21 at cols 5-6 / 8-9 / 30-31 / 33-34.
    const uint8_t *v = g.vram();
    int p1 = 0, p2 = 0;
    for (int r = 6; r < 22; ++r) {
        for (int c : {5, 6, 8, 9}) if (v[r * 80 + c * 2]) ++p1;
        for (int c : {30, 31, 33, 34}) if (v[r * 80 + c * 2]) ++p2;
    }
    MESSAGE("yin-yang pixels: P1 slots " << p1 << "  P2 slots " << p2);
    CHECK(p1 > 0);
    CHECK(p2 > 0);
}

TEST_CASE("fist: the real .dsk boots and shows the HUD")
{
    // The GUI's configuration: one .dsk on disk 0 with its own STARTS.COM.
    std::string dsk = fist::dskPath();
    if (!fist::fs::exists(dsk)) { MESSAGE("fist_game.dsk absent - skipping"); return; }
    FistGame g(dsk, 2900);
    for (int i = 0; i < 100; ++i) { g.poke(0xAA01, 2); g.poke(0xAA41, 2); g.step(); }
    const uint8_t *v = g.vram();
    int hud = 0;
    for (int r = 6; r < 22; ++r)
        for (int c : {5, 6, 8, 9, 30, 31, 33, 34})
            if (v[r * 80 + c * 2]) ++hud;
    MESSAGE("HUD slot pixels in the top strip: " << hud);
    CHECK(hud > 0);
    // The six-digit BCD score ($B02D) draws across the strip; two different
    // values must produce different pixels.
    g.poke(0xB02D, 0); g.poke(0xB02E, 0); g.poke(0xB02F, 0);
    g.settle(40);
    auto sZero = scoreSig(g);
    g.poke(0xB02D, 0x34); g.poke(0xB02E, 0x12); g.poke(0xB02F, 0x56);
    g.settle(40);
    auto sVal = scoreSig(g);
    MESSAGE("score-region pixel sum: zero=" << sZero.second << " nonzero=" << sVal.second);
    CHECK(sVal.second > 0);
    CHECK(sVal.first != sZero.first);
}

TEST_CASE("fist: loading screen, attract demo, fire starts the game")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    FistGame g("fist_intro_lib", 300);
    // The loading screen is the first non-blank VRAM once the loader runs; it
    // holds ~3 s, so sampling every 10 frames catches it.
    int titleAt = -1, titleNz = 0;
    while (g.frame() < 1400) {
        g.step();
        if (titleAt < 0 && g.frame() % 10 == 0) {
            int nz = g.vramNonzero();
            if (nz > 3000) {
                titleAt = g.frame(); titleNz = nz;
                std::string out = fist::opt("intro-out");
                if (!out.empty()) g.dumpVram(out);
            }
        }
    }
    MESSAGE("loading screen first seen at frame " << titleAt << " (nz=" << titleNz << ")");
    CHECK(titleAt > 0);
    // After the hold: the attract demo, both fighters computer-controlled with
    // random personalities 7..10.
    CHECK(g.gst(0xAA06) == 1);
    CHECK(g.gst(0xAA46) == 1);
    int id1 = g.gst(0xAA94), id2 = g.gst(0xAA80);
    MESSAGE("demo AI personalities: P1 " << id1 << " P2 " << id2);
    CHECK((id1 >= 7 && id1 <= 10));
    CHECK((id2 >= 7 && id2 <= 10));
    // Fire -> the 1-player game: P1 human, novice, opponent 0, background 2.
    g.startGame();
    CHECK(g.gst(0xAA06) == 0);
    CHECK(g.gst(0xAA46) == 1);
    CHECK(g.gst(0xB05F) == 0);
    CHECK(g.gst(0xAA80) == 0);
    CHECK(g.gst(0xAF34) == 2);
}

namespace {

struct Combo { ms0515::Key first, second; int move; const char *name; };

/* The port's control map (fire = Space; forward = towards the opponent),
 * P1 facing right.  Keypad keys, Space / Shift / Ctrl as fire, and arrow
 * chords (the keyboard repeats only the last key; the chord rule keeps the
 * earlier one held) in either order. */
const Combo kCombos[] = {
    {ms0515::Key::Kp8, ms0515::Key::Kp8, 5, "KP8 jump"},
    {ms0515::Key::Kp2, ms0515::Key::Kp2, 4, "KP2 crouch"},
    {ms0515::Key::Kp6, ms0515::Key::Kp6, 2, "KP6 forward"},
    {ms0515::Key::Kp4, ms0515::Key::Kp4, 3, "KP4 back"},
    {ms0515::Key::Kp9, ms0515::Key::Kp9, 9, "KP9 forward somersault"},
    {ms0515::Key::Kp7, ms0515::Key::Kp7, 8, "KP7 backward somersault"},
    {ms0515::Key::Kp3, ms0515::Key::Kp3, 10, "KP3 foot sweep"},
    {ms0515::Key::Kp1, ms0515::Key::Kp1, 16, "KP1 reverse sweep"},
    {ms0515::Key::Kp8, ms0515::Key::Space, 6, "Space+KP8 high punch"},
    {ms0515::Key::Kp2, ms0515::Key::Space, 7, "Space+KP2 low punch"},
    {ms0515::Key::Kp6, ms0515::Key::Space, 12, "Space+KP6 front kick"},
    {ms0515::Key::Kp4, ms0515::Key::Space, 13, "Space+KP4 roundhouse"},
    {ms0515::Key::Kp3, ms0515::Key::Space, 11, "Space+KP3 low kick"},
    {ms0515::Key::Kp9, ms0515::Key::Space, 14, "Space+KP9 flying kick"},
    {ms0515::Key::Kp7, ms0515::Key::Space, 15, "Space+KP7 reverse high kick"},
    {ms0515::Key::Kp1, ms0515::Key::Space, 17, "Space+KP1 spinning back kick"},
    {ms0515::Key::Right, ms0515::Key::ShiftL, 12, "Shift+RIGHT front kick"},
    {ms0515::Key::Up, ms0515::Key::Ctrl, 6, "Ctrl+UP high punch"},
    {ms0515::Key::Up, ms0515::Key::Right, 9, "UP then RIGHT forward somersault"},
    {ms0515::Key::Right, ms0515::Key::Up, 9, "RIGHT then UP forward somersault"},
    {ms0515::Key::Down, ms0515::Key::Right, 10, "DOWN then RIGHT foot sweep"},
    {ms0515::Key::Left, ms0515::Key::Up, 8, "LEFT then UP backward somersault"},
    {ms0515::Key::Down, ms0515::Key::Left, 16, "DOWN then LEFT reverse sweep"},
    {ms0515::Key::Space, ms0515::Key::Up, 6, "Space then UP high punch"},
    {ms0515::Key::Space, ms0515::Key::Down, 7, "Space then DOWN low punch"},
    {ms0515::Key::Space, ms0515::Key::Left, 13, "Space then LEFT roundhouse"},
    {ms0515::Key::Space, ms0515::Key::Right, 12, "Space then RIGHT front kick"},
};

/* Press the combo from a known idle stance; true if $AA05 (the selected
 * move) shows the expected move within 30 frames.  Move 7 is re-staged as
 * action 24 at once. */
bool comboSeen(FistGame &g, const Combo &c)
{
    g.emu.keyReleaseAll();
    g.settle(30);                                   // let the previous move play out
    g.resetFighters();
    g.settle(5);
    g.emu.keyPress(c.first, true);
    g.settle(1);
    bool chord = c.second != c.first;
    if (chord) g.emu.keyPress(c.second, true);
    bool seen = false;
    for (int i = 0; i < 30 && !seen; ++i) {
        g.step();
        int mv = g.gst(0xAA05);
        seen = mv == c.move || (c.move == 7 && mv == 24);
    }
    if (chord) g.emu.keyPress(c.second, false);
    g.emu.keyPress(c.first, false);
    MESSAGE(std::string(c.name) << " -> move " << c.move << std::string(seen ? " ok" : " MISSING")
            << " (AA05=" << (int)g.gst(0xAA05) << " AA04=" << (int)g.gst(0xAA04) << ")");
    return seen;
}

}  // namespace

TEST_CASE("fist: keyboard drives P1 - walking, the 16 moves, fire alone")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    FistGame g("fist_keys_lib");
    g.startGame();
    CHECK(g.gst(0xAA06) == 0);                      // P1 human
    g.parkP2();
    // A held direction walks (the keyboard auto-repeats, KSCAN keeps the hold).
    int xBase = g.gst(0xAA19);
    g.emu.keyPress(ms0515::Key::Right, true);
    g.settle(400);
    int xRight = g.gst(0xAA19);
    g.emu.keyPress(ms0515::Key::Right, false);
    g.settle(80);
    g.emu.keyPress(ms0515::Key::Left, true);
    g.settle(400);
    int xLeft = g.gst(0xAA19);
    g.emu.keyPress(ms0515::Key::Left, false);
    MESSAGE("P1 x ($AA19): baseline=" << xBase << "  after RIGHT=" << xRight
            << "  after LEFT=" << xLeft);
    CHECK(xRight > xBase);
    CHECK(xLeft < xRight);
    for (const Combo &c : kCombos)
        CHECK(comboSeen(g, c));
    // Fire alone does nothing ($98DD: idle).
    g.emu.keyReleaseAll();
    g.settle(80);
    g.resetFighters();
    g.settle(10);
    g.emu.keyPress(ms0515::Key::Space, true);
    int maxAct = 1;
    for (int i = 0; i < 12; ++i) { g.step(); maxAct = std::max(maxAct, (int)g.gst(0xAA04)); }
    g.emu.keyPress(ms0515::Key::Space, false);
    MESSAGE("fire alone: max P1 action " << maxAct << " (1 = idle)");
    CHECK(maxAct == 1);
}

TEST_CASE("fist: a real fight scores, decides rounds, holds and resets")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    FistGame g("fist_score_lib");
    g.startGame();
    CHECK(g.gst(0xAA01) == 0);
    CHECK(g.gst(0xAA41) == 0);
    // P1 alternates attacking (Space) and closing in (Right) against the AI.
    int peak = 0, changes = 0, prev = 0;
    int roundsDecided = 0, roundsReset = 0, holdLen = 0, maxHold = 0;
    bool decided = false;
    for (int i = 0; i < 24000; ++i) {
        bool atk = (i / 40) % 2 == 0;
        g.emu.keyPress(ms0515::Key::Space, atk);
        g.emu.keyPress(ms0515::Key::Right, !atk);
        g.step();
        int s = g.gst(0xAA01) + g.gst(0xAA41);
        peak = std::max(peak, std::max((int)g.gst(0xAA01), (int)g.gst(0xAA41)));
        if (s != prev) { ++changes; prev = s; }
        // A decided round (two yin-yang, or the clock with a score on the board)
        // holds its final frame while the winner bows, then NEWRND resets.
        bool won4 = g.gst(0xAA01) >= 4 || g.gst(0xAA41) >= 4;
        if (!decided && (won4 || g.gst(0x9C2B)) && s > 0) { decided = true; ++roundsDecided; holdLen = 0; }
        if (decided) {
            ++holdLen;
            if (s == 0 && g.gst(0x9CA5) == 30) {
                decided = false; ++roundsReset;
                maxHold = std::max(maxHold, holdLen);
            }
        }
    }
    MESSAGE("peak yin-yang=" << peak << " score-changes=" << changes
            << " rounds decided=" << roundsDecided << " reset=" << roundsReset
            << " max hold=" << maxHold << " frames");
    CHECK(changes > 0);
    CHECK(peak >= 2);
    CHECK(roundsDecided >= 1);
    CHECK(maxHold > 60);
    CHECK(roundsReset >= 1);
}

TEST_CASE("fist: the dojo follows the rank, game over returns to novice")
{
    if (!fist::built()) { MESSAGE("FIST not built - skipping"); return; }
    std::vector<std::vector<uint8_t>> expect(4);
    for (int ref = 1; ref <= 3; ++ref) {
        fist::fs::path p = fist::fs::path(fist::expectDir()) / ("bg" + std::to_string(ref) + "_vram.bin");
        if (!fist::fs::exists(p)) { MESSAGE("bg_expect.py dumps absent - skipping"); return; }
        expect[ref] = fist::readFile(p);
        REQUIRE(expect[ref].size() >= 200u * 80u);
    }
    FistGame g("fist_bg_lib");
    CHECK(g.gst(0xAF34) == 2);                      // a game opens on bg 2 ($AC59)
    CHECK(g.gst(0xB05F) == 0);                      // a novice
    CHECK(g.gst(0xAA3C) == 2);                      // two rounds per opponent
    CHECK(bgMismatches(g, expect[2]) == 0);
    // Two won rounds beat an opponent: RANKTK advances $AF34, the dan climbs,
    // RENDBG redraws the dojo after the round-end hold.
    int ref = 2;
    for (int opp = 0; opp < 4; ++opp) {
        int before = g.gst(0xAF34);
        REQUIRE(p1Wins(g, 3000, [&] { return g.gst(0xAF34) != before; }));
        ref = ref % 3 + 1;                          // $AF27: 1..3, 4 -> 1
        CHECK(g.gst(0xAF34) == ref);
        CHECK(g.gst(0xB05F) == opp + 1);
        CHECK(g.gst(0xAA3C) == 2);
        g.settle(120);
        int bad = bgMismatches(g, expect[ref]);
        MESSAGE("opponent " << opp << ": $AF34=" << (int)g.gst(0xAF34) << " dan="
                << (int)g.gst(0xB05F) << " static-row mismatches vs bg" << ref << " = " << bad);
        CHECK(bad == 0);
    }
    CHECK((g.gst(0xB02D) | g.gst(0xB02E) | g.gst(0xB02F)) != 0);   // the clock paid out
    g.settle(1500);
    CHECK(bgMismatches(g, expect[ref]) == 0);       // and it stays stable
    // P2 wins a round -> game over -> the demo: novice again, score 0, bg 2.
    bool over = false;
    for (int i = 0; i < 3000 && !over; ++i) {
        if (g.gst(0xAA41) < 4) {
            g.poke(0xAA41, 2); g.poke(0xAA48, 2);
            g.poke(0xAA43, 0x10); g.poke(0xAA03, 0);
            g.poke(0x9C28, 1);
        }
        g.step();
        over = g.gst(0xB05F) == 0 && g.gst(0xAA41) == 0;
    }
    REQUIRE(over);
    CHECK(g.gst(0xAF34) == 2);
    CHECK((g.gst(0xB02D) | g.gst(0xB02E) | g.gst(0xB02F)) == 0);
    g.settle(120);
    CHECK(bgMismatches(g, expect[2]) == 0);
}
