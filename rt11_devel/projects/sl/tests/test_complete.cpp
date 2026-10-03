/*
 * test_complete.cpp - Tab completes the monitor's commands.
 *
 * The names come from KMON's own command table: a unique match is
 * completed; several are completed as far as they agree and listed at
 * once, the prompt and the line shown again under the list; a Tab right
 * after a Tab only beeps.  (A word after the command is a file name:
 * test_files.cpp.)
 */
#include "SlMachine.hpp"

namespace {

/* Some row of the screen holds all of `names`, in that order. */
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

TEST_SUITE("SL completion") {

TEST_CASE("a unique command is completed and runs") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYP\t");
    CHECK(m.shown() == ".TYPE");
    m.type(" STARTS.COM\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("SET SL ON") == 1);
}

TEST_CASE("several: one Tab completes as far as they agree and lists them") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("FO\t");
    INFO("screen:\n" << m.screen());
    CHECK(listed(m, {"FORTRAN", "FORMAT"}));
    CHECK(m.shown() == ".FOR");                 /* the prompt and the line again */
    m.type("M\t");
    CHECK(m.shown() == ".FORMAT");
}

TEST_CASE("Tab on an empty line lists every command") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("\t");
    INFO("screen:\n" << m.screen());
    CHECK(listed(m, {"DIRECTORY", "TYPE"}));
    CHECK(listed(m, {"MAKE", "MUNG"}));
    CHECK(m.shown() == ".");
}

TEST_CASE("Tabs after a Tab only beep: the list comes once") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("DI\t");
    CHECK(m.shown() == ".DI");                  /* DIRECTORY, DIBOL, DIFFERENCES */
    CHECK(listed(m, {"DIRECTORY", "DIBOL", "DIFFERENCES"}));
    const auto once = m.screen();
    m.type("\t\t\t");
    CHECK(m.screen() == once);                  /* nothing printed, nothing typed */
    m.type("R\t");                             /* after a key, Tab works again */
    CHECK(m.shown() == ".DIRECTORY");
    m.type("\t");
    CHECK(m.shown() == ".DIRECTORY");           /* complete: a Tab only beeps */
}

TEST_CASE("after a program has run - over the memory completion used - Tab still works") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("TYP\t");
    CHECK(m.shown() == ".TYPE");
    m.ctrl('U');
    m.type("DIR\r");                            /* DIR.SAV fills the user area */
    m.type("TYP\t");
    CHECK(m.shown() == ".TYPE");
}

TEST_CASE("nothing matches: the line stays") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("QQ\t\t");
    CHECK(m.shown() == ".QQ");
}

}  /* TEST_SUITE */
