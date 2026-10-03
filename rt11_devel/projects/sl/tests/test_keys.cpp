/*
 * test_keys.cpp - the six keys over the arrows, and PF2.
 *
 * A VT52 has no such keys: the ROM sends ESC [ n ~ for them, and DEC's SL
 * took the tail for text.  Here Find (a PC's Home) goes to the start of the
 * line, Select (End) to its end, Remove (Delete) deletes under the cursor;
 * Insert, Prev and Next are taken and do nothing.  PF2 shows the keys from
 * the overlay.
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
}

}  // namespace

TEST_SUITE("SL editing keys") {

TEST_CASE("Find and Select: the start and the end of the line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("ATE 1-JAN-9");
    m.key(K::Find);
    m.type("D");
    m.key(K::Select);
    m.type("9");
    runsTheLine(m);
}

TEST_CASE("Remove deletes the character under the cursor") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DXATE 1-JAN-99");
    m.key(K::Find);
    m.key(K::Right);
    m.key(K::Remove);
    m.key(K::Select);
    m.key(K::Remove);                           /* at the end: nothing to delete */
    runsTheLine(m);
}

TEST_CASE("Insert, Prev and Next put nothing into the line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-");
    m.key(K::Insert);
    m.key(K::Prev);
    m.key(K::Next);
    INFO("screen:\n" << m.screen());
    CHECK(m.shown() == ".DATE 1-");
    m.type("JAN-99");
    runsTheLine(m);
}

TEST_CASE("PF2 shows the keys and gives the line back") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE");
    m.key(K::Pf2);
    m.settle(1500, 150);                        /* the overlay is read: long and silent */
    const auto help = m.screen();
    INFO("help:\n" << help);
    CHECK(help.find("Tab         complete") != std::string::npos);
    CHECK(help.find("Home End") != std::string::npos);
    m.type(" 1-JAN-99\r");                      /* the key after the help is a key of the line */
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual(kLine) == 1);
    m.type("DATE\r");
    CHECK(m.rowsEqual("1-Jan-99") == 1);
}

TEST_CASE("PF2 after an error tells of the error, as DEC's does") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.key(K::Left);                             /* at the left margin: an error */
    m.key(K::Pf2);
    INFO("screen:\n" << m.screen());
    CHECK(m.screen().find("At left margin now") != std::string::npos);
}

TEST_CASE("in a program's line PF2 is DEC's line, and the keys still edit") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("R PIP");
    m.tap(K::Return);
    m.waitShown("*");
    REQUIRE(m.shown() == "*");
    m.key(K::Pf2);
    INFO("screen:\n" << m.screen());
    CHECK(m.screen().find("See System User's Guide") != std::string::npos);
    m.type("T:=STARTS.COM");
    m.key(K::Find);
    m.type("T");
    m.tap(K::Return);
    m.waitShown("*");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("*TT:=STARTS.COM") == 1);
}

}
