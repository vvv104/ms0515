/*
 * test_kstack.cpp - the kernel stacks keep a margin.  Each process has 320
 * bytes of kernel stack in bank 6 (proc.h KSTACK), a canary in its bottom
 * word that swtch() checks; the deepest calls measured took 242.  Bank 6 is
 * cleared at the boot, so a stack's untouched part is still zero: after a
 * heavy run - pipelines over swap, writing, mkfs and mount, a script - the
 * lowest 32 bytes above each canary are.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

/* high.h's layout: the mount table first, then the processes, each with
 * its kernel stack at its end (proc.h). */
constexpr unsigned kHighBase = 0140000, kMounts = 162, kProc = 38, kKstack = 320, kProcs = 4;
constexpr unsigned kMargin = 32;

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the kernel stacks keep a margin under the heaviest calls") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.runUntil("login:", 3000));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.waitPrompt());
    for (const char *c : {"ls /bin", "cat /etc/numbers | cat | cat", "echo a > /tmp/x", "cat < /tmp/x",
                          "mkdir /tmp/d", "echo b > /tmp/d/y", "ls /tmp/d", "rm /tmp/d/y", "pid &", "pid",
                          "echo ls /tmp > /tmp/s", "sh /tmp/s", "umount /dev/rd0", "mkfs /dev/rd0",
                          "mount /dev/rd0 /tmp", "cat /etc/numbers | cat | cat | cat", "xyz", "ls /dev"}) {
        INFO(c << "\n" << m.screen());
        REQUIRE(m.command(c, 3000));
    }
    INFO(m.screen());
    CHECK(m.screen().find("panic") == std::string::npos);
    for (unsigned p = 0; p < kProcs; ++p) {
        const unsigned bottom = kHighBase + kMounts + p * (kProc + kKstack) + kProc;
        CAPTURE(p);
        unsigned touched = 0;
        for (unsigned a = bottom + 2; a < bottom + 2 + kMargin; a += 2)
            touched |= m.emulator().readWord(static_cast<uint16_t>(a));
        CHECK(touched == 0);
    }
}

}
