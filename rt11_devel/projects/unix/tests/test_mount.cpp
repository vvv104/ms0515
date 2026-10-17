/*
 * test_mount.cpp - stage 5, volumes: the block devices in /dev, mkfs on the
 * machine, mount and umount, a path into a mounted volume and out of it by
 * ".." - on the image without /etc/rc, nothing mounted at the boot; the RAM disk's volume then read on the host from the board's
 * memory, by the tests' own reading of the format.
 */
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(1500));
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

/* The RAM disk formatted on its lower half and mounted at /tmp. */
void ramVolume(Machine &m)
{
    REQUIRE(m.command("mkfs /dev/rd0 512"));
    REQUIRE(m.screen().find("mkfs: /dev/rd0: 512 blocks") != std::string::npos);
    REQUIRE(m.command("mount /dev/rd0 /tmp"));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("/dev has the diskettes' sides and drives and the RAM disk, block devices") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage());
    atPrompt(m);
    REQUIRE(m.command("ls /dev"));
    INFO(m.screen());
    CHECK(after(m, "ls /dev").find("fd0  fd1  fd2  fd3  fdx0  fdx1  rd0") != std::string::npos);
    Volume v(m.imageNow());
    const auto rd0 = v.lookup("/dev/rd0");
    REQUIRE(rd0);
    CHECK((v.inode(*rd0).mode & 0170000) == 0060000);
    CHECK(v.inode(*rd0).start[0] == 6);
}

TEST_CASE("mkfs makes a volume on the RAM disk, mount puts it at /tmp, a file goes on it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), true);
    atPrompt(m);
    ramVolume(m);
    REQUIRE(m.command("echo in ram > /tmp/f"));
    REQUIRE(m.command("ls /tmp"));
    REQUIRE(m.command("cat /tmp/f"));
    INFO(m.screen());
    CHECK(after(m, "ls /tmp").find("\nf\n#") != std::string::npos);
    CHECK(after(m, "cat /tmp/f").find("\nin ram\n#") != std::string::npos);
    /* On the RAM disk, as the host reads the board's memory: mkfs.py's
     * format, the file at its root. */
    Volume rv(m.ramDisk(), true);
    CHECK(rv.word(1, 0) == 054125);
    CHECK(rv.word(1, 4) == 512);
    CHECK(rv.word(1, 24) == 11);                    /* the data from block 11 */
    const auto f = rv.lookup("/f");
    REQUIRE(f);
    CHECK(rv.content(rv.inode(*f)) == "in ram\n");
    /* And not on the diskette: its /tmp is empty. */
    Volume dv(m.imageNow());
    REQUIRE(dv.lookup("/tmp"));
    CHECK_FALSE(dv.lookup("/tmp/f"));
}

TEST_CASE(".. from the mounted volume's root comes out to the directory above it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), true);
    atPrompt(m);
    ramVolume(m);
    REQUIRE(m.command("cat /tmp/../etc/motd"));
    REQUIRE(m.command("ls /tmp/.."));
    INFO(m.screen());
    CHECK(after(m, "cat /tmp/../etc/motd").find("Welcome to UNIX") != std::string::npos);
    CHECK(after(m, "ls /tmp/..").find("etc") != std::string::npos);
}

TEST_CASE("umount takes the volume out: /tmp is the diskette's again; mounted anew, the file is back") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), true);
    atPrompt(m);
    ramVolume(m);
    REQUIRE(m.command("echo kept in ram > /tmp/k"));
    REQUIRE(m.command("umount /dev/rd0"));
    REQUIRE(m.command("cat /tmp/k"));
    CHECK(m.screen().find("cat: /tmp/k: not found") != std::string::npos);
    REQUIRE(m.command("mount /dev/rd0 /tmp"));
    REQUIRE(m.command("cat /tmp/k"));
    INFO(m.screen());
    CHECK(after(m, "cat /tmp/k").find("\nkept in ram\n#") != std::string::npos);
}

TEST_CASE("mount refuses what is not a volume, a device mounted twice, and a file for a directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), true);
    atPrompt(m);
    REQUIRE(m.command("mount /dev/rd0 /tmp"));             /* not formatted yet */
    REQUIRE(m.command("mount /etc/motd /tmp"));
    ramVolume(m);
    REQUIRE(m.command("mount /dev/rd0 /tmp"));
    INFO(m.screen());
    CHECK(m.screen().find("mount: /dev/rd0: no file system there") != std::string::npos);
    CHECK(m.screen().find("mount: /etc/motd: not a block device") != std::string::npos);
    CHECK(m.screen().find("mount: /dev/rd0: busy") != std::string::npos);
}

}
