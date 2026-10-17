/*
 * test_rc.cpp - stage 5, the boot by /etc/rc: init runs the shell on the
 * script, mount -a makes the RAM disk a volume at /tmp and turns swap on by
 * /etc/fstab - the RAM disk's file, else the diskette's - and getty asks for
 * the name; a session's end brings getty back, not the script.
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
    REQUIRE(m.runUntil("login:", 3000));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.waitPrompt());
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("with the RAM disk: /tmp is its volume, swap a file on it, before login:") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.runUntil("login:", 3000));
    const std::string boot = m.screen();
    INFO(boot);
    CHECK(boot.find("mount: /dev/rd0 on /tmp") != std::string::npos);
    CHECK(boot.find("swap: /tmp/swap") != std::string::npos);
    CHECK(boot.find("mount: /dev/rd0 on /tmp") < boot.find("login:"));
    atPrompt(m);
    REQUIRE(m.command("ls /tmp"));
    CHECK(after(m, "ls /tmp").find("\nswap\n$") != std::string::npos);
    REQUIRE(m.command("echo a | cat"));
    CHECK(after(m, "echo a | cat").find("\na\n$") != std::string::npos);
}

TEST_CASE("without the RAM disk: no volume at /tmp, swap the diskette's /swap") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.runUntil("login:", 3000));
    const std::string boot = m.screen();
    INFO(boot);
    CHECK(boot.find("mount:") == std::string::npos);    /* optional: no word */
    CHECK(boot.find("swap: /swap") != std::string::npos);
    CHECK(boot.find("swap: /tmp/swap") == std::string::npos);
}

TEST_CASE("the session's end brings getty back, and /etc/rc is not run again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    m.typeAndSettle("exit\r", 200);
    REQUIRE(m.runUntil("login:", 1500));
    m.run(50);
    INFO(m.screen());
    CHECK(count(m.screen(), "login:") == 2);
    CHECK(count(m.screen(), "mount: /dev/rd0") == 1);
}

TEST_CASE("a mounted device is not written under its volume: mkfs is refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("mkfs /dev/rd0"));
    REQUIRE(m.command("ls /tmp"));
    INFO(m.screen());
    CHECK(m.screen().find("mkfs: /dev/rd0: busy") != std::string::npos);
    CHECK(after(m, "ls /tmp").find("swap") != std::string::npos);
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
    CHECK(after(m, "sh /tmp/s").find("\none\n$") != std::string::npos);
}

}
