/*
 * test_dirs.cpp - a process's own directory: chdir, the shell's cd, paths
 * from it when they do not begin with /, its children's the same; pwd by
 * stat, walking .. up to the root, out of a mounted volume too.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

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

void says(Machine &m, const std::string &command, const std::string &text)
{
    REQUIRE(m.command(command, 3000));
    INFO(m.screen());
    CAPTURE(command);
    CHECK(after(m, command).find("\n" + text + "\n$") != std::string::npos);
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("cd and pwd: paths from a process's own directory, .. up, its children's the same") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    says(m, "pwd", "/");
    REQUIRE(m.command("mkdir /d1"));
    REQUIRE(m.command("mkdir /d1/d2"));
    REQUIRE(m.command("cd /d1/d2"));
    says(m, "pwd", "/d1/d2");
    REQUIRE(m.command("echo here > f"));
    says(m, "cat /d1/d2/f", "here");
    says(m, "cat f | cat", "here");                   /* the children are where the shell is */
    REQUIRE(m.command("cd .."));
    says(m, "pwd", "/d1");
    says(m, "ls", "d2");
    says(m, "cat d2/f", "here");
    REQUIRE(m.command("cd"));                         /* home: the root */
    says(m, "pwd", "/");
    Volume v(m.imageNow());
    CHECK(v.lookup("/d1/d2/f"));
}

TEST_CASE("cd into a mounted volume and out of it by ..; pwd across the mount; what is no directory refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);              /* the RAM disk at /tmp */
    atPrompt(m);
    REQUIRE(m.command("cd /tmp"));
    says(m, "pwd", "/tmp");
    REQUIRE(m.command("mkdir t"));
    REQUIRE(m.command("cd t"));
    says(m, "pwd", "/tmp/t");
    REQUIRE(m.command("cd ../.."));
    says(m, "pwd", "/");
    REQUIRE(m.command("cd /nothere"));
    CHECK(after(m, "cd /nothere").find("sh: /nothere: not found") != std::string::npos);
    REQUIRE(m.command("cd /etc/motd"));
    CHECK(after(m, "cd /etc/motd").find("sh: /etc/motd: not a directory") != std::string::npos);
    says(m, "pwd", "/");
}

}
