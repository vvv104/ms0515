/*
 * test_rc.cpp - stage 5, the boot by /etc/rc: init runs the shell on the
 * script, mount -a makes the RAM disk a volume at /tmp by /etc/fstab, and
 * the shell takes its place; a session's end brings a new shell, not the
 * script.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

std::size_t count(const std::string &text, const std::string &what)
{
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("with the RAM disk: the root its copy, /tmp with it, before the shell's prompt") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot(3000));
    const std::string boot = m.screen();
    INFO(boot);
    CHECK(boot.find("ramroot: the system into /dev/rd0") != std::string::npos);
    CHECK(boot.find("ramroot: the system into /dev/rd0") < boot.rfind("#"));
    atPrompt(m);
    REQUIRE(m.command("ls /tmp"));
    INFO(m.screen());
    CHECK(after(m, "ls /tmp").find_first_not_of(" \n#") == std::string::npos);   /* an empty volume */
    REQUIRE(m.command("echo a | cat"));
    CHECK(after(m, "echo a | cat").find("\na\n#") != std::string::npos);
}

TEST_CASE("without the RAM disk: no volume at /tmp, the diskette's directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot(3000));
    const std::string boot = m.screen();
    INFO(boot);
    CHECK(boot.find("mount:") == std::string::npos);    /* optional: no word */
    CHECK(boot.find("swap") == std::string::npos);
}

TEST_CASE("the session's end brings a new shell in /home, and /etc/rc is not run again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("cd /etc"));
    REQUIRE(m.command("exit"));
    REQUIRE(m.command("pwd"));
    INFO(m.screen());
    CHECK(after(m, "pwd").find("\n/home\n#") != std::string::npos);
    CHECK(count(m.screen(), "ramroot:") == 1);
}

TEST_CASE("a mounted device is not written under its volume: mkfs is refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("echo kept > /tmp/k"));
    REQUIRE(m.command("mkfs /dev/rd0"));
    REQUIRE(m.command("ls /tmp"));
    INFO(m.screen());
    CHECK(m.screen().find("mkfs: /dev/rd0: busy") != std::string::npos);
    CHECK(after(m, "ls /tmp").find("\nk\n#") != std::string::npos);
}

TEST_CASE("sh runs a script: a file of commands, without a prompt") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("echo echo one > /tmp/s"));
    REQUIRE(m.command("cat /tmp/s"));
    /* /etc/rc is such a script, its comments passed by: the boot shows it. */
    REQUIRE(m.command("sh /tmp/s"));
    INFO(m.screen());
    CHECK(after(m, "sh /tmp/s").find("\none\n#") != std::string::npos);
}

}
