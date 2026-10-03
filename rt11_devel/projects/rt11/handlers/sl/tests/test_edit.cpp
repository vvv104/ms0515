/*
 * test_edit.cpp - editing the command line with DEC's keys.
 *
 * Every case edits a broken "DATE 1-JAN-99" into the right one and presses
 * Return.  The row the line leaves on the screen is the proof the display
 * followed the edits; the date the monitor then prints is the proof the
 * edited line is what reached it.
 */
#include "SlMachine.hpp"

using K = ms0515::Key;

namespace {

constexpr const char *kLine = ".DATE 1-JAN-99";

/* Return, and the monitor ran exactly DATE 1-JAN-99. */
void runsTheLine(sl::SlMachine &m)
{
    m.type("\r");
    INFO("screen after Return:\n" << m.screen());
    CHECK(m.rowsEqual(kLine) == 1);
    m.type("DATE\r");
    INFO("screen after DATE:\n" << m.screen());
    CHECK(m.rowsEqual("1-Jan-99") == 1);
    CHECK(m.atPrompt());
}

}  // namespace

TEST_SUITE("SL editing") {

TEST_CASE("characters insert at the cursor: Left and Right") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DTE 1-JAN-9");
    for (int i = 0; i < 10; ++i) m.key(K::Left);
    m.type("A");
    for (int i = 0; i < 10; ++i) m.key(K::Right);
    m.type("9");
    runsTheLine(m);
}

TEST_CASE("the ends of the line: GOLD Left and GOLD Right") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("ATE 1-JAN-9");
    m.gold(K::Left);
    m.type("D");
    m.gold(K::Right);
    m.type("9");
    runsTheLine(m);
}

TEST_CASE("Delete rubs out the character before the cursor") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    SUBCASE("at the end") {
        m.type("DATE 1-JAN-98");
        m.key(K::Backspace);
        m.type("9");
    }
    SUBCASE("in the middle") {
        m.type("DATTE 1-JAN-99");
        for (int i = 0; i < 10; ++i) m.key(K::Left);
        m.key(K::Backspace);
    }
    SUBCASE("and GOLD Delete puts it back") {
        m.type("DATE 1-JAN-99");
        m.key(K::Backspace);
        m.gold(K::Backspace);
    }
    runsTheLine(m);
}

TEST_CASE("Ctrl/U deletes to the start of the line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("RUBBISHDATE 1-JAN-99");
    for (int i = 0; i < 13; ++i) m.key(K::Left);
    m.ctrl('U');
    runsTheLine(m);
}

TEST_CASE("PF3 deletes to the end of the line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99RUBBISH");
    for (int i = 0; i < 7; ++i) m.key(K::Left);
    m.key(K::Pf3);
    runsTheLine(m);
}

TEST_CASE("Ctrl/R shows the line again") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99");
    m.ctrl('R');
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == kLine);
    runsTheLine(m);
}

}
