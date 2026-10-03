/*
 * test_files.cpp - Tab completes file names.
 *
 * A word after the command - after a blank, a comma or an equals sign -
 * is a file: its name completes from the directory of its device (DK:
 * when none is typed), read by SL itself.  One match is completed, several
 * as far as they agree and listed.
 */
#include "SlMachine.hpp"

namespace {

/* Every name appears on the screen, in a row of its own list. */
bool shownAll(sl::SlMachine &m, std::initializer_list<const char *> names)
{
    const auto s = m.screen();
    for (const char *n : names)
        if (s.find(n) == std::string::npos) return false;
    return true;
}

}  // namespace

TEST_SUITE("SL files") {

TEST_CASE("a unique file name is completed and the command runs") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYPE STAR\t");
    CHECK(m.shown() == ".TYPE STARTS.COM");
    m.type("\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("SET SL ON") == 1);
}

TEST_CASE("an extension typed in part is completed too") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYPE STARTS.C\t");
    CHECK(m.shown() == ".TYPE STARTS.COM");
}

TEST_CASE("several files: as far as they agree, and the list") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DIR D\t");
    CHECK(m.shown() == ".DIR D");               /* DZ.SYS DV.SYS DIR.SAV DUP.SAV */
    INFO("screen:\n" << m.screen());
    CHECK(shownAll(m, {"DZ.SYS", "DV.SYS", "DIR.SAV", "DUP.SAV"}));
    CHECK(m.shown() == ".DIR D");
    m.type("U\t");
    CHECK(m.shown() == ".DIR DUP.SAV");
}

TEST_CASE("a device typed with the name, and a file after a comma or an equals sign") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYPE SY:STA\t");
    CHECK(m.shown() == ".TYPE SY:STARTS.COM");
    m.ctrl('U');
    m.type("COPY X.TMP=RES\t");
    CHECK(m.shown() == ".COPY X.TMP=RESORC.SAV");
    m.ctrl('U');
    m.type("DIR/FULL PIP.SAV,DZ0:DU\t");
    CHECK(m.shown() == ".DIR/FULL PIP.SAV,DZ0:DUP.SAV");
}

TEST_CASE("no such file, no such device: the line stays") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYPE QQQ\t\t");
    CHECK(m.shown() == ".TYPE QQQ");
    m.ctrl('U');
    m.type("TYPE QQ:A\t\t");
    CHECK(m.shown() == ".TYPE QQ:A");
    m.ctrl('U');
    m.type("TYPE STARTS.COM\r");                /* and the machine is fine */
    CHECK(m.rowsEqual("SET SL ON") == 1);
}

}  /* TEST_SUITE */
