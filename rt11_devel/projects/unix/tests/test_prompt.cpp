/*
 * test_prompt.cpp - the shell's line: its prompt tells the directory it is
 * in, "/home# ", and Tab completes the word in hand.  The kernel hands the
 * line to the shell at a Tab, keeping it, and takes the completion back as
 * if typed (TTY_TAB, kernel/ioctl.h): BS goes back over it, Enter runs the
 * whole.  The first word is completed from /bin, the others from the path
 * they begin; several names that go on alike are listed and the line drawn
 * again.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;

namespace {

/* The last line of the screen as shown, its trailing spaces cut. */
std::string lastLine(Machine &m)
{
    std::string s = m.shown();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    const auto nl = s.rfind('\n');
    return s.substr(nl == std::string::npos ? 0 : nl + 1);
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the prompt tells the shell's directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    CHECK(lastLine(m) == "/home#");
    REQUIRE(m.command("cd /etc"));
    CHECK(lastLine(m) == "/etc#");
    REQUIRE(m.command("cd .."));
    CHECK(lastLine(m) == "/#");
    REQUIRE(m.command("cd tmp"));                       /* the RAM disk's volume */
    CHECK(lastLine(m) == "/tmp#");
    REQUIRE(m.command("cd nowhere"));
    CHECK(lastLine(m) == "/tmp#");
    REQUIRE(m.command("cd"));
    CHECK(lastLine(m) == "/home#");
}

TEST_CASE("Tab completes a command from /bin, the line then run whole") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("ech\t", 30);
    INFO(m.shown());
    CHECK(lastLine(m) == "/home# echo");
    REQUIRE(m.command("hi"));
    CHECK(after(m, "echo hi").find("\nhi\n#") != std::string::npos);
}

TEST_CASE("Tab completes a path: a directory with its /, a file with a space") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat /e\t", 30);
    CHECK(lastLine(m) == "/home# cat /etc/");
    m.typeAndSettle("mo\t", 30);
    INFO(m.shown());
    CHECK(lastLine(m) == "/home# cat /etc/motd");
    REQUIRE(m.command(""));
    CHECK(after(m, "cat /etc/motd").find("Welcome to UBIX") != std::string::npos);
    REQUIRE(m.command("cd /etc"));
    m.typeAndSettle("cat num\t", 30);                    /* from the shell's directory */
    CHECK(lastLine(m) == "/etc# cat numbers");
}

TEST_CASE("BS goes back over a completion as over what was typed") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat /etc/mo\t", 30);
    m.typeAndSettle("\b\b\b\b", 30);                     /* "otd " back */
    CHECK(lastLine(m) == "/home# cat /etc/m");
    REQUIRE(m.command("otd"));
    INFO(m.shown());
    CHECK(after(m, "cat /etc/motd").find("Welcome to UBIX") != std::string::npos);
}

TEST_CASE("names that go on alike are listed, the line drawn again; none, nothing changes") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("ls /bin/m\t", 50);
    INFO(m.shown());
    const std::string s = m.shown();
    CHECK(s.find("mkdir") != std::string::npos);
    CHECK(s.find("mount") != std::string::npos);
    CHECK(lastLine(m) == "/home# ls /bin/m");
    m.typeAndSettle("ou\t", 30);
    CHECK(lastLine(m) == "/home# ls /bin/mount");
    m.typeAndSettle("\b\b\b\b\b\bzz\t", 30);             /* "mount " back */
    CHECK(lastLine(m) == "/home# ls /bin/zz");
    REQUIRE(m.command(""));
    CHECK(after(m, "ls /bin/zz").find("not found") != std::string::npos);
}

}
