/*
 * test_mkboot.cpp - mkboot DEVICE: the system's volume, the root's, copied
 * to a diskette that then boots - its boot block and /boot/kernel's blocks
 * where the superblock says, its size the diskette's, a volume number of
 * its own.  The root in the RAM disk or on the diskette alike; into the
 * second drive, or into the boot drive once its diskette is let go and
 * another put in.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

/* The clone booted alone, without a RAM disk: the shell at its prompt and
 * a file of the system read. */
void boots(const std::vector<uint8_t> &clone)
{
    Machine c(clone, false);
    REQUIRE(c.boot());
    REQUIRE(c.command("cat /etc/motd"));
    INFO(c.screen());
    CHECK(after(c, "cat /etc/motd").find("Welcome to UNIX") != std::string::npos);
}

unsigned superWord(const std::vector<uint8_t> &image, int word)
{
    Volume v(image);
    return v.word(1, 2 * word);                                /* fs.h superblock word */
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("mkboot from the RAM root to the second drive: the copy boots") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto image = unixos::readImage();
    Machine m(image, true);
    m.insertSecond(std::vector<uint8_t>(819200, 0));
    REQUIRE(m.boot());
    REQUIRE(m.command("echo mine > /home/mine"));
    REQUIRE(m.command("mkboot /dev/fdx1", 6000));
    INFO(m.screen());
    CHECK(after(m, "mkboot /dev/fdx1").find("mkboot: /dev/fdx1: 1600 blocks") != std::string::npos);
    const auto clone = m.secondNow();
    CHECK(superWord(clone, 2) == 1600);                       /* its size */
    CHECK(superWord(clone, 3) == superWord(image, 3));        /* the kernel's blocks where they were */
    CHECK(superWord(clone, 4) == superWord(image, 4));
    CHECK(Volume(clone).longAt(1, 28) != Volume(image).longAt(1, 28));   /* a volume number of its own */
    Volume v(clone);
    REQUIRE(v.lookup("/home/mine"));
    CHECK(v.content(v.inode(*v.lookup("/home/mine"))) == "mine\n");
    boots(clone);
}

TEST_CASE("mkboot to one side: an 800-block system that boots") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    m.insertSecond(std::vector<uint8_t>(409600, 0));
    REQUIRE(m.boot());
    REQUIRE(m.command("mkboot /dev/fd2", 6000));
    INFO(m.screen());
    CHECK(after(m, "mkboot /dev/fd2").find("mkboot: /dev/fd2: 800 blocks") != std::string::npos);
    const auto clone = m.secondNow();
    CHECK(superWord(clone, 2) == 800);
    boots(clone);
}

TEST_CASE("mkboot with the root on the diskette: the copy boots") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(std::vector<uint8_t>(819200, 0));
    REQUIRE(m.boot());
    REQUIRE(m.command("mkboot /dev/fdx1", 6000));
    INFO(m.screen());
    CHECK(after(m, "mkboot /dev/fdx1").find("mkboot: /dev/fdx1: 1600 blocks") != std::string::npos);
    boots(m.secondNow());
}

TEST_CASE("mkboot refused: the root's own diskette, a mounted one, no diskette, no device") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("mkboot /dev/fdx0"));
    CHECK(after(m, "mkboot /dev/fdx0").find("mkboot: /dev/fdx0: busy") != std::string::npos);
    REQUIRE(m.command("mkboot /dev/fd1"));
    CHECK(after(m, "mkboot /dev/fd1").find("mkboot: /dev/fd1: busy") != std::string::npos);
    REQUIRE(m.command("mkboot /dev/fdx1", 6000));                /* an empty drive */
    CHECK(after(m, "mkboot /dev/fdx1").find("mkboot: /dev/fdx1: ") != std::string::npos);
    REQUIRE(m.command("mkboot /etc/motd"));
    CHECK(after(m, "mkboot /etc/motd").find("mkboot: /etc/motd: not a block device") != std::string::npos);
    REQUIRE(m.command("mkboot"));
    CHECK(after(m, "mkboot").find("usage: mkboot DEVICE") != std::string::npos);
    INFO(m.screen());
    REQUIRE(m.command("ls /bin"));
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

TEST_CASE("one drive: the system in memory, its diskette let go, a blank one put in and made the system") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("umount /dev/fdx0"));
    m.insertFirst(std::vector<uint8_t>(819200, 0));
    REQUIRE(m.command("mkboot /dev/fdx0", 6000));
    INFO(m.screen());
    CHECK(after(m, "mkboot /dev/fdx0").find("mkboot: /dev/fdx0: 1600 blocks") != std::string::npos);
    boots(m.firstNow());
}

}
