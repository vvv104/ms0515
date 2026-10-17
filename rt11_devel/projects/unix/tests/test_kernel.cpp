/*
 * test_kernel.cpp - the kernel is one file, /boot/kernel: its code in the
 * low banks, its data in bank 6.  The calls that once were a second part's
 * - mount, umount, ioctl, lseek, a program that owns the machine - are
 * its own.
 */
#include "Machine.hpp"
#include "Volume.hpp"

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
    REQUIRE(m.runUntil("login:", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("mount: /dev/rd0 on /tmp") != std::string::npos);   /* /etc/rc's mount -a */
}

}
