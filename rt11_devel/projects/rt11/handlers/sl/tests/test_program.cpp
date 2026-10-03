/*
 * test_program.cpp - SL in a program's command line, and its help.
 *
 * A program that reads its command through the CSI gets the same editor
 * as the monitor.  PF2 is HELP: DEC's picture of the keys, drawn with the
 * VT52's own cursor control, and the line back after any key.
 */
#include "SlMachine.hpp"

using K = ms0515::Key;

TEST_SUITE("SL in programs") {

TEST_CASE("PIP's command line is edited") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("R PIP");
    m.tap(K::Return);
    m.waitShown("*");
    INFO("screen:\n" << m.screen());
    REQUIRE(m.shown() == "*");
    m.type("TT:=STRTS.COM");
    for (int i = 0; i < 7; ++i) m.key(K::Left);
    m.type("A");
    m.tap(K::Return);
    m.waitShown("*");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("*TT:=STARTS.COM") == 1);
    CHECK(m.rowsEqual("SET SL ON") == 1);       /* the file, typed out */
    CHECK(m.shown() == "*");
}

TEST_CASE("PF2 shows the keys and gives the line back") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE");
    m.key(K::Pf2);
    m.settle(600, 40);
    const auto help = m.screen();
    INFO("help:\n" << help);
    /* The plain legends; the GOLD ones are in reverse video, which the
     * screen reader does not read. */
    CHECK(help.find("|GET OLD|") != std::string::npos);
    CHECK(help.find("|CTRL W |CTRL R |CTRL U |") != std::string::npos);
    CHECK(help.find('[') == std::string::npos);     /* no VT100 sequence printed */
    CHECK(m.shown() == ".DATE");                    /* the line, under the picture */
    m.type(" 1-JAN-99\r");
    m.type("DATE\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("1-Jan-99") == 1);
}

}
