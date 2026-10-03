/*
 * test_switches.cpp - Tab completes a command's switches.
 *
 * After a '/' the word is one of the command's switches - the lists are
 * KMON's own, read from its overlays on the disk by SET SL ON (they are
 * not in memory while a line is edited).  The command may be abbreviated as KMON
 * allows it; the switch may follow the command or a file name.
 */
#include "SlMachine.hpp"

namespace {

bool listed(sl::SlMachine &m, std::initializer_list<const char *> names)
{
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        const auto row = m.row(r);
        size_t at = 0;
        bool all = true;
        for (const char *n : names) {
            at = row.find(n, at);
            if (at == std::string::npos) { all = false; break; }
            at += std::string(n).size();
        }
        if (all) return true;
    }
    return false;
}

}  // namespace

TEST_SUITE("SL switches") {

TEST_CASE("a unique switch is completed after the command") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DIR/FU\t");
    CHECK(m.shown() == ".DIR/FULL");
    m.type("/OC\t");
    CHECK(m.shown() == ".DIR/FULL/OCTAL");
}

TEST_CASE("the command's full name and an abbreviation both find the list") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DIRECTORY/BR\t");
    CHECK(m.shown() == ".DIRECTORY/BRIEF");
    m.ctrl('U');
    m.type("TY STARTS.COM/NE\t");
    CHECK(m.shown() == ".TY STARTS.COM/NEWFILES");
}

TEST_CASE("DIR/ Tab lists DIR's switches") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DIR/\t");
    INFO("screen:\n" << m.screen());
    CHECK(listed(m, {"PROTECTION", "BLOCKS", "POSITION", "BRIEF", "FAST"}));
    CHECK(listed(m, {"BACKUP"}));
    CHECK(m.shown() == ".DIR/");
}

TEST_CASE("several switches: as far as they agree, and the list") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DIR/F\t");
    CHECK(m.shown() == ".DIR/F");               /* FAST FULL FREE FILES */
    CHECK(listed(m, {"FAST", "FULL", "FREE", "FILES"}));
    m.type("R\t");
    CHECK(m.shown() == ".DIR/FREE");
}

TEST_CASE("switches after a switch and after a file name") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("COPY/LO\t");
    CHECK(m.shown() == ".COPY/LOG");
    m.type("/QU\t");
    CHECK(m.shown() == ".COPY/LOG/QUERY");
    m.type(" A.MAC B.MAC/REP\t");
    CHECK(m.shown() == ".COPY/LOG/QUERY A.MAC B.MAC/REPLACE");
}

TEST_CASE("a completed switch runs") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYPE/LO\t");
    CHECK(m.shown() == ".TYPE/LOG");
    m.type(" STARTS.COM\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("SET SL ON") == 1);
}

TEST_CASE("a command without switches, or none matching: the line stays") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("SET/\t\t");
    CHECK(m.shown() == ".SET/");
    m.ctrl('U');
    m.type("DIR/QQ\t\t");
    CHECK(m.shown() == ".DIR/QQ");
}

TEST_CASE("the lists are the running monitor's: MOUNT's WRITE, which no copy had") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("MOUNT/WR\t");
    CHECK(m.shown() == ".MOUNT/WRITE");
}

}  /* TEST_SUITE */
