/*
 * test_more.cpp - more, a page at a time: 23 lines and --More--, a key
 * read from the console one at a time (the console's raw mode, TTY_RAW) -
 * space the next page, Enter the next line, q the end.  Its input a file
 * or a pipe; its output not the console, the text goes through whole.
 * Ctrl+C takes it out, and the console is the shell's line again.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

bool has(Machine &m, const std::string &s)
{
    return m.screen().find(s) != std::string::npos;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("more: a page, then space for the next, the prompt after the last") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("more /etc/numbers\r", 300);
    INFO(m.screen());
    CHECK(has(m, "line 23:"));
    CHECK_FALSE(has(m, "line 24:"));
    CHECK(has(m, "--More--"));
    m.typeAndSettle(" ", 100);
    CHECK(has(m, "line 40:"));
    REQUIRE(m.waitPrompt(200));
    CHECK_FALSE(has(m, "--More--"));
}

TEST_CASE("more: Enter a line at a time, q the end") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat /etc/numbers | more\r", 400);
    INFO(m.screen());
    CHECK(has(m, "line 23:"));
    m.typeAndSettle("\r", 50);
    CHECK(has(m, "line 24:"));
    CHECK_FALSE(has(m, "line 25:"));
    m.typeAndSettle("q", 50);
    REQUIRE(m.waitPrompt(200));
    CHECK_FALSE(has(m, "line 25:"));
    REQUIRE(m.command("echo typed"));                         /* the console's line again */
    CHECK(has(m, "\ntyped\n#"));
}

TEST_CASE("more: a short file whole, the output not the console whole, Ctrl+C out of it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("more /etc/motd", 300));
    CHECK(has(m, "Welcome to UBIX"));
    REQUIRE(m.command("more /etc/numbers > /home/m", 3000));
    Volume v(m.imageNow());
    REQUIRE(v.lookup("/home/m"));
    CHECK(v.content(v.inode(*v.lookup("/home/m"))) == v.content(v.inode(*v.lookup("/etc/numbers"))));
    m.typeAndSettle("more /etc/numbers\r", 300);
    m.typeAndSettle("\x03", 0);
    REQUIRE(m.waitPrompt(200));
    REQUIRE(m.command("echo after"));
    INFO(m.screen());
    CHECK(has(m, "\nafter\n#"));
}

}
