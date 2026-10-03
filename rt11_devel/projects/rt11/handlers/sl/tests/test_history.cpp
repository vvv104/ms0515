/*
 * test_history.cpp - the lines SL keeps.
 *
 * DEC's SL keeps two lines back - Up gets the previous one, GOLD Up the
 * one before it - and one line the user saves by hand: GOLD Down saves,
 * Down gets.
 */
#include "SlMachine.hpp"

using K = ms0515::Key;

TEST_SUITE("SL history") {

TEST_CASE("Up gets the previous line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99\r");
    m.key(K::Up);
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == ".DATE 1-JAN-99");
    /* It is a line to edit like any other. */
    m.key(K::Backspace);
    m.type("8\r");
    m.type("DATE\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("1-Jan-98") == 1);
}

TEST_CASE("GOLD Up gets the line before the previous one") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99\r");
    m.type("DATE\r");
    m.gold(K::Up);
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == ".DATE 1-JAN-99");
}

TEST_CASE("GOLD Down saves the line and Down gets it") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99");
    m.gold(K::Down);
    m.ctrl('U');
    m.type("DATE\r");
    m.type("DATE\r");
    m.key(K::Down);
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == ".DATE 1-JAN-99");
}

}
