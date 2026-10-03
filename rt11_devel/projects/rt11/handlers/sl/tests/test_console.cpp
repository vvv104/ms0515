/*
 * test_console.cpp - SL and the ROM's console.
 *
 * The console is a VT52 that prints what it does not know: an ESC [ of a
 * VT100 leaves its tail on the screen.  SL built for the machine sends
 * nothing of the kind - turning it on, prompting and turning it off leave
 * the screen as the monitor alone would.
 */
#include "SlMachine.hpp"

using K = ms0515::Key;

TEST_SUITE("SL and the console") {

TEST_CASE("SET SL ON prints nothing and the prompt is bare") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m{sl::kQuiet};
    m.type("SET SL ON\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual(".SET SL ON") == 1);
    CHECK(m.atPrompt());
    /* Nothing between the command and the prompt but the blank line the
     * monitor leaves. */
    int command = 0;
    while (m.row(command) != ".SET SL ON") ++command;
    CHECK(m.row(command + 1).empty());
    CHECK((m.row(command + 2) == "." || m.row(command + 2) == "._"));
}

TEST_CASE("an empty Return gives a bare prompt each time") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("\r");
    m.type("\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.atPrompt());
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        const auto row = m.row(r);
        if (!row.empty() && row[0] == '.' && row != ".SET SL ON" && row != ".SET TT QUIET")
            CHECK_MESSAGE((row == "." || row == "._"), "row " << r << ": " << row);
    }
}

TEST_CASE("SET SL OFF gives the line back to the monitor") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("SET SL OFF\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.atPrompt());
    /* The monitor's own line: a Left arrow is not an edit any more, the
     * line is not DATE and the monitor says so. */
    m.type("DTE");
    m.key(K::Left);
    m.key(K::Left);
    m.type("A\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual(".DATE") == 0);
}

}
