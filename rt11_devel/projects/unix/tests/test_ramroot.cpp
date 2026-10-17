/*
 * test_ramroot.cpp - the root in the RAM disk.  With the EX board /etc/rc
 * runs /bin/ramroot first: the blocks the boot diskette's volume has taken
 * copied to the RAM disk, the copy made the root (mount(device, "/")), the
 * diskette's volume put at /disk.  So the system runs from memory and the
 * diskette is let go by umount and taken out; the files written to the
 * root are the RAM disk's, those under /disk the diskette's.  Without the
 * board the root stays the diskette's.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("with the RAM disk the system is copied there and the root is the copy") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    int frames = 0;
    for (; frames < 3000 && !m.waitPrompt(10); frames += 10) {}
    MESSAGE("booted with the root in the RAM disk in " << frames << " frames");
    INFO(m.screen());
    REQUIRE(frames < 3000);
    CHECK(m.screen().find("ramroot: the root in /dev/rd0, the diskette at /disk") != std::string::npos);
    REQUIRE(m.command("echo in memory > /home/f"));
    REQUIRE(m.command("cat /home/f"));
    CHECK(after(m, "cat /home/f").find("\nin memory\n#") != std::string::npos);
    Volume rd(m.ramDisk(), true);
    CHECK(rd.lookup("/home/f"));
    CHECK(rd.lookup("/bin/sh"));
    CHECK(rd.lookup("/etc/motd"));
    CHECK(rd.lookup("/disk"));
    Volume fd(m.imageNow());
    CHECK_FALSE(fd.lookup("/home/f"));
    CHECK_FALSE(fd.lookup("/disk"));
}

TEST_CASE("the diskette at /disk: its files read and written there") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("ls /disk/bin"));
    CHECK(after(m, "ls /disk/bin").find("sh") != std::string::npos);
    REQUIRE(m.command("cp /home/../etc/motd /disk/home/saved", 3000));
    INFO(m.screen());
    Volume fd(m.imageNow());
    const auto saved = fd.lookup("/home/saved"), motd = fd.lookup("/etc/motd");
    REQUIRE(saved);
    REQUIRE(motd);
    CHECK(fd.content(fd.inode(*saved)) == fd.content(fd.inode(*motd)));
}

TEST_CASE("the diskette let go and taken out: the system runs on from memory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("umount /dev/fdx0"));
    CHECK(after(m, "umount /dev/fdx0").find("umount:") == std::string::npos);
    m.emulator().unmountDisk(0);
    m.emulator().unmountDisk(2);
    REQUIRE(m.command("cat /etc/motd", 3000));
    CHECK(after(m, "cat /etc/motd").find("Welcome to UNIX") != std::string::npos);
    REQUIRE(m.command("echo x | cat", 3000));
    CHECK(after(m, "echo x | cat").find("\nx\n#") != std::string::npos);
    REQUIRE(m.command("ls /bin", 3000));
    INFO(m.screen());
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
    CHECK(m.screen().find("panic") == std::string::npos);
}

TEST_CASE("the root's device is not made anew: mkfs of the RAM disk refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("mkfs /dev/rd0"));
    INFO(m.screen());
    CHECK(after(m, "mkfs /dev/rd0").find("busy") != std::string::npos);
    REQUIRE(m.command("ls /bin"));
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

TEST_CASE("without the RAM disk the root stays the diskette's") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    int frames = 0;
    for (; frames < 3000 && !m.waitPrompt(10); frames += 10) {}
    MESSAGE("booted with the root on the diskette in " << frames << " frames");
    INFO(m.screen());
    REQUIRE(frames < 3000);
    CHECK(m.screen().find("ramroot") == std::string::npos);
    REQUIRE(m.command("echo on the diskette > /home/f"));
    Volume fd(m.imageNow());
    CHECK(fd.lookup("/home/f"));
    CHECK_FALSE(fd.lookup("/disk"));
}

}
