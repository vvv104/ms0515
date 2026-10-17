/*
 * test_own.cpp - a program that owns the machine (SPEC.md stage 7): an
 * a.out of its own magic, 0401, run by its name as any other.  exec lays
 * it over everything, the kernel too, from 001000 up to 157000; it talks
 * to the ROM through its little library, and its end is a reset: the
 * system boots again.
 */
#include "Machine.hpp"

#include <string>
#include <vector>

using unixos::Machine;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("bigmem owns the machine: all of 001000..150000 its own, the ROM its console and disk, a reset its end") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    m.typeAndSettle("bigmem\r", 0);
    REQUIRE(m.runUntil("PRESS A KEY", 3000));
    const std::string s = m.screen();
    INFO(s);
    /* Its pattern written and read back over the kernel's code, the slots
     * and the kernel's tables alike. */
    CHECK(s.find("BIGMEM") != std::string::npos);
    CHECK(s.find("-150000 OK") != std::string::npos);
    /* The root's superblock read through the ROM's 160010: the magic. */
    CHECK(s.find("SUPER 54125") != std::string::npos);
    m.typeAndSettle("q", 0);
    /* The reset: the screen the ROM's again, and the diskette booted anew. */
    int frames = 0;
    for (; frames < 3000 && m.screen().find("PRESS A KEY") != std::string::npos; frames += 10) m.run(10);
    CHECK(frames < 3000);
    REQUIRE(m.runUntil("kernel: the diskettes by /boot/fd", 3000));
    REQUIRE(m.boot(3000));
    atPrompt(m);
    REQUIRE(m.command("echo again"));
    CHECK(m.screen().find("\nagain\n") != std::string::npos);
}

TEST_CASE("bigmem is given its command line, and reads a file of the root by its own reader through the ROM") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    {
        Machine m(unixos::readImage(), false);
        atPrompt(m);
        m.typeAndSettle("bigmem /etc/numbers two\r", 0);
        REQUIRE(m.runUntil("PRESS A KEY", 3000));
        const std::string s = m.screen();
        INFO(s);
        CHECK(s.find("ARGS 3") != std::string::npos);
        CHECK(s.find("ARG bigmem") != std::string::npos);
        CHECK(s.find("ARG /etc/numbers") != std::string::npos);
        CHECK(s.find("ARG two") != std::string::npos);
        /* The last line: the file's last block, through its extent. */
        CHECK(s.find("LAST line 40: the quick brown fox jumps over the lazy dog") != std::string::npos);
    }
    {
        Machine m(unixos::readImage(), false);
        atPrompt(m);
        m.typeAndSettle("bigmem /etc/none\r", 0);
        REQUIRE(m.runUntil("PRESS A KEY", 3000));
        INFO(m.screen());
        CHECK(m.screen().find("NO FILE") != std::string::npos);
    }
}

TEST_CASE("bigmem from side 1 of the second drive: the loader reads it by the ROM's unit 3") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(std::vector<uint8_t>(819200, 0));
    atPrompt(m);
    REQUIRE(m.command("mkfs /dev/fd3", 3000));
    REQUIRE(m.command("mount /dev/fd3 /tmp"));
    REQUIRE(m.command("cat /bin/bigmem > /tmp/b", 3000));
    REQUIRE(m.command("chmod r-x /tmp/b"));                   /* a copy is rw- */
    m.typeAndSettle("/tmp/b\r", 0);
    REQUIRE(m.runUntil("PRESS A KEY", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("-150000 OK") != std::string::npos);
    CHECK(m.screen().find("SUPER 54125") != std::string::npos);    /* the root's, unit 0 */
}

TEST_CASE("a program that owns the machine is refused where the ROM cannot read it: on the RAM disk") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("cat /bin/bigmem > /tmp/bigmem"));
    REQUIRE(m.command("chmod r-x /tmp/bigmem"));
    REQUIRE(m.command("/tmp/bigmem"));
    INFO(m.screen());
    CHECK(m.screen().find("/tmp/bigmem: cannot run") != std::string::npos);
    CHECK(m.screen().find("PRESS A KEY") == std::string::npos);
    REQUIRE(m.command("echo still"));
    CHECK(m.screen().find("\nstill\n") != std::string::npos);
}

}
