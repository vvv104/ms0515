/*
 * test_release.cpp - UBIX as it is given out: the kernel says the system's
 * name and version first, as RT-11 does; ubix.dsk, both sides, and
 * ubix01.dsk, one side, hold the system without the tests' programs and
 * data.  ubix.dsk boots with the RAM disk and without; ubix01.dsk, for
 * the other emulators, has no /boot/rd and so no RAM disk even with the
 * board.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

std::vector<uint8_t> release(const char *name)
{
    std::ifstream in(unixos::imagePath().parent_path() / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the kernel says UBIX and its version first") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    const std::string s = m.screen();
    INFO(s);
    const auto banner = s.find("UBIX V0.1");
    REQUIRE(banner != std::string::npos);
    CHECK(banner < s.find("kernel:"));
    REQUIRE(m.command("cat /etc/motd"));
    CHECK(after(m, "cat /etc/motd").find("Welcome to UBIX") != std::string::npos);
}

TEST_CASE("ubix.dsk and ubix01.dsk: the system, not the tests' programs and data") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    for (const char *name : {"ubix.dsk", "ubix01.dsk"}) {
        CAPTURE(name);
        const auto image = release(name);
        REQUIRE(image.size() == (std::string(name) == "ubix.dsk" ? 819200u : 409600u));
        Volume v(image);
        CHECK(bool(v.lookup("/boot/rd")) == (std::string(name) == "ubix.dsk"));
        for (const char *path : {"/boot/kernel", "/boot/fd", "/etc/init", "/etc/rc", "/etc/fstab",
                                 "/etc/motd", "/bin/sh", "/bin/ed", "/bin/grep", "/bin/rt11", "/bin/mkboot",
                                 "/bin/ramroot", "/home", "/tmp", "/mnt/floppy1", "/mnt/floppy2"}) {
            CAPTURE(path);
            CHECK(v.lookup(path));
        }
        for (const char *path : {"/bin/hello", "/bin/fail", "/bin/pid", "/bin/bigmem", "/etc/numbers"}) {
            CAPTURE(path);
            CHECK_FALSE(v.lookup(path));
        }
    }
}

TEST_CASE("ubix01.dsk, for the other emulators: no RAM disk even with the board") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto image = release("ubix01.dsk");
    for (bool ram : {false, true}) {
        CAPTURE(ram);
        Machine m(image, ram);
        REQUIRE(m.boot());
        INFO(m.screen());
        CHECK(m.screen().find("kernel: no RAM disk") != std::string::npos);
        CHECK(m.screen().find("ramroot") == std::string::npos);
        REQUIRE(m.command("echo ok"));
        CHECK(after(m, "echo ok").find("\nok\n#") != std::string::npos);
    }
}

TEST_CASE("ubix.dsk boots with the RAM disk and without, ubix01.dsk without") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    for (const char *name : {"ubix.dsk", "ubix01.dsk"})
        for (bool ram : {true, false}) {
            if (ram && std::string(name) == "ubix01.dsk")
                continue;
            CAPTURE(name);
            CAPTURE(ram);
            Machine m(release(name), ram);
            REQUIRE(m.boot());
            INFO(m.screen());
            CHECK(m.screen().find("UBIX V0.1") != std::string::npos);
            CHECK((m.screen().find("ramroot: the root in /dev/rd0") != std::string::npos) == ram);
            REQUIRE(m.command("echo kept > /home/f"));
            REQUIRE(m.command("ls /bin | grep -c ed", 4000));      /* through /tmp on the diskette without one */
            CHECK(after(m, "ls /bin | grep -c ed").find("\n1\n#") != std::string::npos);
            Volume v(m.imageNow());
            CHECK(v.lookup("/home/f"));                     /* on the diskette either way */
        }
}

}
