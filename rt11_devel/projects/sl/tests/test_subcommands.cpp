/*
 * test_subcommands.cpp - Tab completes the words of SET and SHOW.
 *
 * SHOW's keywords, SET's subjects (KMON's own - EDIT, WILDCARDS, ERROR,
 * TERMINAL/TT, KMON, EXIT, USR - and the installed devices whose handlers
 * have SET options) and a subject's options: all read from the running
 * monitor and the handlers on SY: by SET SL ON.  KMON compares a subject
 * by its first three letters, so that is how SL offers and finds them.
 */
#include "SlMachine.hpp"

namespace {

bool shownAll(sl::SlMachine &m, std::initializer_list<const char *> names)
{
    const auto s = m.screen();
    for (const char *n : names)
        if (s.find(n) == std::string::npos) return false;
    return true;
}

}  // namespace

TEST_SUITE("SL subcommands") {

TEST_CASE("SHOW's keywords") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("SHOW DEV\t");
    CHECK(m.shown() == ".SHOW DEVICES");
    m.ctrl('U');
    m.type("SHOW C\t");
    INFO("screen:\n" << m.screen());
    CHECK(shownAll(m, {"CONFIGURATION", "COMMANDS"}));
    CHECK(m.shown() == ".SHOW CO");
    m.type("N\t");
    CHECK(m.shown() == ".SHOW CONFIGURATION");
}

TEST_CASE("SET's subjects: KMON's own and the devices with options") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("SET \t");
    INFO("screen:\n" << m.screen());
    CHECK(shownAll(m, {"EDI", "WIL", "ERR", "TER", "KMO", "EXI", "USR", "TT", "SL", "LD"}));
    m.type("WI\t");
    CHECK(m.shown() == ".SET WIL");
}

TEST_CASE("a subject's options, the NO forms too, and after a comma") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("SET TT Q\t");
    CHECK(m.shown() == ".SET TT QUIET");
    m.type(",NOH\t");
    CHECK(m.shown() == ".SET TT QUIET,NOHOLD");
    m.ctrl('U');
    m.type("SET TERMINAL NOS\t");
    CHECK(m.shown() == ".SET TERMINAL NOSCOPE");
    m.ctrl('U');
    m.type("SET EDIT K\t");
    CHECK(shownAll(m, {"KED", "K52"}));
    m.ctrl('U');
    m.type("SET SL TT\t");                      /* our own handler's table */
    CHECK(m.shown() == ".SET SL TTYIN");
    m.ctrl('U');
    m.type("SET LD0 CL\t");                     /* a unit: the device's table */
    CHECK(m.shown() == ".SET LD0 CLEAN");
}

TEST_CASE("a completed subcommand runs") {
    if (!sl::built()) { MESSAGE("SL.SYS not built - skipped"); return; }
    sl::SlMachine m;
    m.type("SET TT NOQ\t");
    CHECK(m.shown() == ".SET TT NOQUIET");
    m.type("\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.screen().find('?') == std::string::npos);
    m.type("SHOW TER\t");
    CHECK(m.shown() == ".SHOW TERMINALS");
}

}  /* TEST_SUITE */
