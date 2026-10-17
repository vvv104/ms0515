/*
 * test_cache.cpp - the buffer cache in the shadow copy of bank 1: a block
 * given up by the few buffers in bank 6 is kept there, so a file read again
 * comes without the disk; fork's copy between the slots, which passes
 * through the same shadow bank, leaves the cache's blocks as they were.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

int diskReads = 0;

/* The ROM's disk entry counted: every block the kernel reads or writes. */
void countDisk(struct ms0515_cpu *) { ++diskReads; }

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

/* The command's output, up to the next prompt. */
std::string output(Machine &m, const std::string &command)
{
    const std::string s = after(m, command);
    return s.substr(0, s.find("\n#"));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("a file read again comes from the cache: not a block from the disk") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    atPrompt(m);
    m.emulator().setExecHook(0160010, countDisk);
    /* /etc/numbers is 5 blocks, cat 4: with the directories and the inodes
     * more than bank 6's buffers hold, less than the shadow bank does. */
    REQUIRE(m.command("cat /etc/numbers"));
    const int first = diskReads;
    diskReads = 0;
    REQUIRE(m.command("cat /etc/numbers"));
    const int second = diskReads;
    m.emulator().setExecHook(0160010, nullptr);
    CAPTURE(first);
    CHECK(first > 0);
    CHECK(second == 0);
}

TEST_CASE("fork's copy through the shadow bank leaves the cached blocks whole") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    atPrompt(m);
    REQUIRE(m.command("cat /etc/motd"));
    const std::string motd = output(m, "cat /etc/motd");
    REQUIRE(m.command("cat /etc/numbers"));
    REQUIRE(m.command("ls /bin"));
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
    REQUIRE(m.command("pid"));
    REQUIRE(m.command("cat /etc/motd"));
    INFO(m.screen());
    CHECK(output(m, "cat /etc/motd") == motd);
    CHECK(motd.find("Welcome") != std::string::npos);
}

}
