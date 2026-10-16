/*
 * test_hi.cpp - the kernel's second part, /boot/hi (kernel/hi.h): read in
 * once the root is mounted and called through its header - mount, umount,
 * ioctl, a program that owns the machine.  One linked against another
 * kernel is said so and kept out: its calls fail, the system runs.
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

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the system's /boot/hi is its kernel's: taken in without a word, mount by it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.runUntil("login:", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("/boot/hi") == std::string::npos);
    CHECK(m.screen().find("mount: /dev/rd0 on /tmp") != std::string::npos);   /* /etc/rc's mount -a */
}

TEST_CASE("a /boot/hi of another kernel is said so and kept out: its calls fail, the system runs") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    {
        Volume v(image);
        const auto h = v.lookup("/boot/hi");
        REQUIRE(h);
        image[v.offset(static_cast<int>(v.inode(*h).start[0])) + 6] ^= 0x10;   /* the key, word 3 */
    }
    Machine m(image, true);
    REQUIRE(m.runUntil("login:", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("UNIX: /boot/hi is not this kernel's") != std::string::npos);
    CHECK(m.screen().find("mount: /dev/rd0 on /tmp") == std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("mount /dev/rd0 /tmp"));
    CHECK(after(m, "mount /dev/rd0 /tmp").find("mount: /dev/rd0: cannot") != std::string::npos);
    REQUIRE(m.command("echo still"));
    CHECK(after(m, "echo still").find("\nstill\n$") != std::string::npos);
}

}
