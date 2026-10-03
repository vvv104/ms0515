/*
 * test_overlay.cpp - where Tab's overlay lives.
 *
 * The overlay is read from SL.SYS when Tab is pressed, into the memory
 * under KMON - 16 blocks under the USR, whose address is in RMON+266 - and
 * the resident handler does not hold it.
 */
#include "SlMachine.hpp"

namespace {

/* The overlay's first two instructions: MOV PC,R1 and ADD #...,R1. */
bool overlayAt(sl::SlMachine &m, uint16_t address)
{
    return m.word(address) == 010701 && m.word(static_cast<uint16_t>(address + 2)) == 062701;
}

/* Where it is, looked for under KMON; 0 when it is not in memory. */
uint16_t overlay(sl::SlMachine &m)
{
    const uint16_t rmon = m.word(054);
    const uint16_t kmon = static_cast<uint16_t>(m.word(static_cast<uint16_t>(rmon + 0266)) - 020000);
    for (uint16_t a = static_cast<uint16_t>(kmon - 014000); a < kmon; a += 2)
        if (overlayAt(m, a)) return a;
    return 0;
}

}  // namespace

TEST_SUITE("SL overlay") {

TEST_CASE("the overlay comes with the first Tab, under KMON") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    CHECK(overlay(m) == 0);
    m.type("TYP\t");
    CHECK(m.shown() == ".TYPE");
    CHECK(overlay(m) != 0);
}

TEST_CASE("a program's line keeps DEC's Tab: a space") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("R PIP");
    m.tap(ms0515::Key::Return);
    m.waitShown("*");
    REQUIRE(m.shown() == "*");
    m.type("TT:\t=STARTS.COM");
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == "*TT: =STARTS.COM");
}

}
