/*
 * test_sides.cpp - the diskettes by sides and drives: fd0 and fd1 the boot
 * drive's two sides, fd2 and fd3 the second drive's, fdx0 and fdx1 the two
 * drives with both sides as one volume.  The system boots from one side, a
 * 409600-byte image as the other emulators take one, or from both; a
 * diskette in the second drive is a volume of its own.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

bool built400()
{
    return unixos::built() && unixos::readImage400().size() == 409600;
}

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the single-sided system boots from one side: swap on its /swap, the programs run") {
    if (!built400()) { MESSAGE("unix400.dsk not built - skipped"); return; }
    Machine m(unixos::readImage400(), false);
    REQUIRE(m.boot(3000));
    CHECK(m.screen().find("swap: /swap") != std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("ls /dev"));
    CHECK(after(m, "ls /dev").find("fd0  fd1  fd2  fd3  fdx0  fdx1  rd0") != std::string::npos);
    REQUIRE(m.command("echo one > /f"));
    REQUIRE(m.command("cat /f"));
    INFO(m.screen());
    CHECK(after(m, "cat /f").find("\none\n$") != std::string::npos);
    Volume v(m.imageNow());
    CHECK(v.word(1, 4) == 800);                       /* the superblock's size */
    CHECK(v.lookup("/f"));
}

TEST_CASE("a diskette in the second drive: made a volume, mounted, written, read on the host") {
    if (!built400()) { MESSAGE("unix400.dsk not built - skipped"); return; }
    Machine m(unixos::readImage400(), false);
    m.insertSecond(std::vector<uint8_t>(409600, 0));
    atPrompt(m);
    REQUIRE(m.command("mkfs /dev/fd2", 3000));
    CHECK(m.screen().find("mkfs: /dev/fd2: 800 blocks") != std::string::npos);
    REQUIRE(m.command("mount /dev/fd2 /tmp"));
    REQUIRE(m.command("echo two > /tmp/g"));
    REQUIRE(m.command("cat /tmp/g"));
    INFO(m.screen());
    CHECK(after(m, "cat /tmp/g").find("\ntwo\n$") != std::string::npos);
    Volume v(m.secondNow());
    const auto g = v.lookup("/g");
    REQUIRE(g);
    CHECK(v.inode(*g).size == 4);
}

TEST_CASE("the double-sided root is fdx0: its sides fd0 and fd1 are busy") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    atPrompt(m);
    REQUIRE(m.command("mkfs /dev/fd1"));
    REQUIRE(m.command("mkfs /dev/fdx0"));
    REQUIRE(m.command("ls /bin"));
    INFO(m.screen());
    CHECK(m.screen().find("mkfs: /dev/fd1: busy") != std::string::npos);
    CHECK(m.screen().find("mkfs: /dev/fdx0: busy") != std::string::npos);
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

TEST_CASE("an empty drive is an error, and the shell goes on") {
    if (!built400()) { MESSAGE("unix400.dsk not built - skipped"); return; }
    Machine m(unixos::readImage400(), false);
    atPrompt(m);
    REQUIRE(m.command("mount /dev/fd2 /tmp", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("mount: /dev/fd2:") != std::string::npos);
    CHECK(m.screen().find("on /tmp") == std::string::npos);
}

}
