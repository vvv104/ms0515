/*
 * test_kernel.cpp - the kernel is one file, /boot/kernel: its code in the
 * low banks, its data in bank 6.  The calls that once were a second part's
 * - mount, umount, ioctl, lseek, a program that owns the machine - are
 * its own.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <cstdlib>
#include <string>

using unixos::Machine;
using unixos::Volume;

TEST_SUITE("UNIX") {

TEST_CASE("the kernel is one file: no /boot/hi, and mount is the kernel's own") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    {
        Volume v(image);
        CHECK(v.lookup("/boot/kernel"));
        CHECK_FALSE(v.lookup("/boot/hi"));
    }
    Machine m(image, true);
    REQUIRE(m.boot(3000));
    INFO(m.screen());
    /* /etc/rc's ramroot: mount(rd0, "/"), the diskette and its /home, the kernel's own mount;
     * uname and mount's table after. */
    /* The copy's blocks told: the system alone, some hundreds of the 1024. */
    const std::string s = m.screen(), copy = "ramroot: the system into /dev/rd0\nramroot: ";
    const auto at = s.find(copy);
    REQUIRE(at != std::string::npos);
    const auto from = at + copy.size();
    const std::string told = s.substr(from, s.find('\n', from) - from);
    INFO(told);
    CHECK(told.find(" of 1024 blocks") != std::string::npos);
    const int n = std::atoi(told.c_str());
    CHECK(n > 100);
    CHECK(n < 600);
    CHECK(m.screen().find("UBIX V0.1\n"
                              "  /dev/rd0          /\n"
                              "  /dev/fdx0         /mnt/floppy1\n"
                              "  /dev/fdx0:/home   /home\n") != std::string::npos);
}

}
