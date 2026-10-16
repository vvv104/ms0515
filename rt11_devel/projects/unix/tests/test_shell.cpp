/*
 * test_shell.cpp - stage 4a: processes.  init asks for the name and gives
 * its place to the shell; the shell runs each command as a child of its own
 * by fork, exec and wait, in the second slot, and comes back with its prompt.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.runUntil("login:", 1500));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.screen().find("Hello, root") != std::string::npos);
    REQUIRE(m.waitPrompt());
}

std::size_t count(const std::string &text, const std::string &what)
{
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

/* What the screen shows after the last time `command` was typed at the prompt. */
std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the shell runs a program and comes back to its prompt, not to login:") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.command("echo one two");
    m.command("hello");
    INFO(m.screen());
    CHECK(after(m, "echo one two").find("one two\n$") != std::string::npos);
    CHECK(after(m, "hello").find("hello, world\n$") != std::string::npos);
    CHECK(count(m.screen(), "login:") == 1);
}

TEST_CASE("a command is looked for in /bin, or taken by its path; one not there is reported") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.command("/bin/echo by path");
    m.command("nope");
    m.command("echo still here");
    INFO(m.screen());
    CHECK(m.screen().find("by path") != std::string::npos);
    CHECK(m.screen().find("nope: not found") != std::string::npos);
    CHECK(after(m, "echo still here").find("still here\n$") != std::string::npos);
}

TEST_CASE("ls and cat are programs of /bin now") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.command("ls /bin");
    m.command("cat /etc/motd");
    m.command("cat /etc/nothing");
    INFO(m.screen());
    for (const char *p : {"sh", "ls", "cat", "echo", "hello", "fail", "pid"}) {
        CAPTURE(p);
        CHECK(after(m, "ls /bin").find(p) != std::string::npos);
    }
    CHECK(m.screen().find("Welcome to UNIX on the Elektronika MS 0515.") != std::string::npos);
    CHECK(m.screen().find("cat: /etc/nothing: not found") != std::string::npos);
}

TEST_CASE("wait brings the child's exit status: the shell's status command shows it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.command("fail");
    m.command("status");
    m.command("echo ok");
    m.command("status");
    INFO(m.screen());
    CHECK(after(m, "fail").find("status 3") != std::string::npos);
    CHECK(after(m, "echo ok").find("status 0") != std::string::npos);
}

TEST_CASE("each child has a pid of its own, the shell's the same throughout") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.command("pid");
    const std::string first = after(m, "pid");
    m.command("pid");
    const std::string second = after(m, "pid");
    INFO(m.screen());
    /* pid prints "pid N parent P": the parent, the shell, is pid 1 - init
     * gave it its place - and each child is another. */
    CHECK(first.find("parent 1") != std::string::npos);
    CHECK(second.find("parent 1") != std::string::npos);
    CHECK(first.substr(0, first.find("parent")) != second.substr(0, second.find("parent")));
}

TEST_CASE("the shell's exit ends the session: init asks for the name again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    m.typeAndSettle("exit\r", 200);
    INFO(m.screen());
    CHECK(count(m.screen(), "login:") == 2);
}

}
