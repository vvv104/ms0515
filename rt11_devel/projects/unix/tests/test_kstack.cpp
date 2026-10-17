/*
 * test_kstack.cpp - the kernel stacks keep a margin.  Each process has 320
 * bytes of kernel stack in bank 6 (proc.h KSTACK), a canary in its bottom
 * word that swtch() checks; the deepest calls measured took 242.  Bank 6 is
 * cleared at the boot, so a stack's untouched part is still zero: after a
 * heavy run - pipelines, writing, mkfs and mount, a script - the
 * lowest 32 bytes above each canary are.
 */
#include "Machine.hpp"

#include <string>
#include <vector>

using unixos::Machine;

namespace {

/* The tables lie in bank 6 where the kernel's link put them (high.h):
 * the processes one after another, each with its kernel stack at its end
 * (proc.h), the canary its bottom word. */
constexpr unsigned kBank6 = 0140000, kDriver = 0155000, kProc = 36, kKstack = 320, kProcs = 4;
constexpr unsigned kCanary = 0122525, kMargin = 32;

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the kernel stacks keep a margin under the heaviest calls") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot(3000));
    for (const char *c : {"ls /bin", "cat /etc/numbers | cat", "echo a > /tmp/x", "cat < /tmp/x",
                          "mkdir /tmp/d", "echo b > /tmp/d/y", "ls /tmp/d", "rm /tmp/d/y", "pid &", "pid", "sleep 1 &", "ls /bin",
                          "echo ls /tmp > /tmp/s", "sh /tmp/s", "umount /dev/rd0", "mkfs /dev/rd0",
                          "mount /dev/rd0 /tmp", "cat /etc/numbers | cat | cat", "xyz", "ls /dev"}) {
        INFO(c << "\n" << m.screen());
        REQUIRE(m.command(c, 3000));
    }
    INFO(m.screen());
    CHECK(m.screen().find("panic") == std::string::npos);
    std::vector<unsigned> canaries;
    for (unsigned a = kBank6; a < kDriver; a += 2)
        if (m.emulator().readWord(static_cast<uint16_t>(a)) == kCanary) canaries.push_back(a);
    /* The places used: the run parks the shell once, so all but maybe one. */
    REQUIRE(canaries.size() >= kProcs - 1);
    REQUIRE(canaries.size() <= kProcs);
    for (std::size_t p = 1; p < canaries.size(); ++p) CHECK((canaries[p] - canaries[p - 1]) % (kProc + kKstack) == 0);
    for (std::size_t p = 0; p < canaries.size(); ++p) {
        const unsigned bottom = canaries[p];
        CAPTURE(p);
        unsigned touched = 0;
        for (unsigned a = bottom + 2; a < bottom + 2 + kMargin; a += 2)
            touched |= m.emulator().readWord(static_cast<uint16_t>(a));
        CHECK(touched == 0);
    }
}

}
