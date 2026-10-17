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
    REQUIRE(m.boot(3000));
    INFO(m.screen());
    /* /etc/rc's ramroot: mount(rd0, "/"), the diskette and its /home, the kernel's own mount;
     * uname and mount's table after. */
    CHECK(m.screen().find("ramroot: the system into /dev/rd0") != std::string::npos);
    CHECK(m.screen().find("UBIX V0.1\n"
                              "  /dev/rd0          /\n"
                              "  /dev/fdx0         /mnt/floppy1\n"
                              "  /dev/fdx0:/home   /home\n") != std::string::npos);
}

}
