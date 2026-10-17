/*
 * test_ramroot.cpp - the root in the RAM disk.  With the EX board /etc/rc
 * runs "ramroot /mnt/floppy1 /home" first: the system on the boot diskette
 * - all but what /home and /tmp hold - copied to the RAM disk, the copy
 * made the root (mount(device, "/")), the diskette mounted at
 * /mnt/floppy1 and its /home at /home (mount of a directory of a mounted
 * volume).  So the system runs from memory and the user's files stay on
 * the diskette; with cd out of /home, umount takes both places out and
 * the diskette is taken out.  Without the board the root stays the
 * diskette's.
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

/* The system's image with the user's own on it - /home/u, /tmp/t and
 * /home/e a second name of /bin/echo - written with the root on the
 * diskette. */
std::vector<uint8_t> withUserFiles()
{
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo mine > /home/u"));
    REQUIRE(m.command("mkdir /home/d"));
    REQUIRE(m.command("echo deeper > /home/d/v"));
    REQUIRE(m.command("echo scratch > /tmp/t"));
    REQUIRE(m.command("ln /bin/echo /home/e"));
    return m.imageNow();
}

/* Whether block b is taken in the volume's bitmap. */
bool taken(const Volume &v, unsigned b)
{
    const unsigned bitmap = v.word(1, 14);
    return (v.word(static_cast<int>(bitmap), static_cast<int>(b >> 3 & ~1u)) >> (b & 15)) & 1;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("with the RAM disk the system is copied there, the root is the copy, /home the diskette's") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    int frames = 0;
    for (; frames < 3000 && !m.waitPrompt(10); frames += 10) {}
    MESSAGE("booted with the root in the RAM disk in " << frames << " frames");
    INFO(m.screen());
    REQUIRE(frames < 3000);
    CHECK(m.screen().find("ramroot: the root in /dev/rd0, /dev/fdx0 at /mnt/floppy1, /home at /home") != std::string::npos);
    REQUIRE(m.command("ls /mnt/floppy1"));
    CHECK(after(m, "ls /mnt/floppy1").find("bin") != std::string::npos);
    REQUIRE(m.command("echo kept > /home/f"));
    REQUIRE(m.command("echo in memory > /tmp/g"));
    REQUIRE(m.command("cat /home/f"));
    CHECK(after(m, "cat /home/f").find("\nkept\n#") != std::string::npos);
    Volume rd(m.ramDisk(), true);
    CHECK(rd.lookup("/bin/sh"));
    CHECK(rd.lookup("/etc/motd"));
    CHECK(rd.lookup("/tmp/g"));
    CHECK_FALSE(rd.lookup("/home/f"));
    Volume fd(m.imageNow());
    CHECK(fd.lookup("/home/f"));
    CHECK_FALSE(fd.lookup("/tmp/g"));
}

TEST_CASE("the system alone goes into memory: the user's files stay on the diskette, seen at /home") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto image = withUserFiles();
    Machine m(image, true);
    REQUIRE(m.boot());
    REQUIRE(m.command("cat /home/d/v"));
    CHECK(after(m, "cat /home/d/v").find("\ndeeper\n#") != std::string::npos);
    REQUIRE(m.command("echo hi"));                            /* /bin/echo, its blocks kept */
    CHECK(after(m, "echo hi").find("\nhi\n#") != std::string::npos);
    REQUIRE(m.command("ls /tmp"));
    INFO(m.screen());
    CHECK(after(m, "ls /tmp").find("t") == std::string::npos);
    Volume rd(m.ramDisk(), true), fd(image);
    for (const char *path : {"/home/u", "/home/d", "/home/e", "/tmp/t"}) {
        CAPTURE(path);
        CHECK_FALSE(rd.lookup(path));
    }
    const auto echo = rd.lookup("/bin/echo");
    REQUIRE(echo);
    CHECK(rd.inode(*echo).nlink == 1);                        /* its name in /home not copied */
    const auto u = fd.lookup("/home/u");
    REQUIRE(u);
    CHECK_FALSE(taken(rd, fd.inode(*u).start[0]));            /* the user's blocks not taken in the copy */
    CHECK(taken(rd, rd.inode(*echo).start[0]));
}

TEST_CASE("pwd and .. through a directory mounted from a volume") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(withUserFiles(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("cd /home/d"));
    REQUIRE(m.command("pwd"));
    CHECK(after(m, "pwd").find("\n/home/d\n#") != std::string::npos);
    REQUIRE(m.command("cd ../.."));
    REQUIRE(m.command("ls"));
    INFO(m.screen());
    CHECK(after(m, "ls").find("bin") != std::string::npos);
}

TEST_CASE("the diskette let go and taken out: the system runs on from memory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("umount /dev/fdx0"));                    /* the shell is in /home */
    CHECK(after(m, "umount /dev/fdx0").find("umount: /dev/fdx0: busy") != std::string::npos);
    REQUIRE(m.command("cd /"));
    REQUIRE(m.command("umount /dev/fdx0"));
    CHECK(after(m, "umount /dev/fdx0").find("umount:") == std::string::npos);
    m.emulator().unmountDisk(0);
    m.emulator().unmountDisk(2);
    REQUIRE(m.command("cat /etc/motd", 3000));
    CHECK(after(m, "cat /etc/motd").find("Welcome to UBIX") != std::string::npos);
    REQUIRE(m.command("echo x | cat", 3000));
    CHECK(after(m, "echo x | cat").find("\nx\n#") != std::string::npos);
    REQUIRE(m.command("ls /bin", 3000));
    INFO(m.screen());
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
    CHECK(m.screen().find("panic") == std::string::npos);
}

TEST_CASE("another diskette at /mnt/floppy1 and its /home at /home") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("cd /"));
    REQUIRE(m.command("umount /dev/fdx0"));
    REQUIRE(m.command("ls /mnt/floppy1"));                    /* both places gone */
    CHECK(after(m, "ls /mnt/floppy1").find("bin") == std::string::npos);
    m.insertFirst(withUserFiles());
    REQUIRE(m.command("mount /dev/fdx0 /mnt/floppy1", 3000));
    REQUIRE(m.command("mount /mnt/floppy1/home /home", 3000));
    REQUIRE(m.command("cat /home/u", 3000));
    INFO(m.screen());
    CHECK(after(m, "cat /home/u").find("\nmine\n#") != std::string::npos);
    REQUIRE(m.command("cd"));
    REQUIRE(m.command("pwd"));
    CHECK(after(m, "pwd").find("\n/home\n#") != std::string::npos);
}

TEST_CASE("a directory of a mounted volume put elsewhere; what is refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(unixos::readImage());
    REQUIRE(m.boot());
    REQUIRE(m.command("mount /mnt/floppy2/etc /tmp", 3000));      /* not mounted yet: none there */
    CHECK(after(m, "mount /mnt/floppy2/etc /tmp").find("mount: /mnt/floppy2/etc: not found") != std::string::npos);
    REQUIRE(m.command("mount /dev/fdx1 /mnt/floppy2", 3000));
    REQUIRE(m.command("mount /mnt/floppy2/etc /tmp", 3000));
    REQUIRE(m.command("ls /tmp", 3000));
    INFO(m.screen());
    CHECK(after(m, "ls /tmp").find("motd") != std::string::npos);
    REQUIRE(m.command("mount /etc/motd /home", 3000));
    CHECK(after(m, "mount /etc/motd /home").find("mount: /etc/motd: not a block device") != std::string::npos);
    REQUIRE(m.command("mount /etc /home", 3000));                  /* the root's own */
    CHECK(after(m, "mount /etc /home").find("mount: /etc: no file system there") != std::string::npos);
    REQUIRE(m.command("mount /dev/fdx1 /home", 3000));             /* whole, a second time */
    CHECK(after(m, "mount /dev/fdx1 /home").find("busy") != std::string::npos);
    REQUIRE(m.command("mount /mnt/floppy2/bin /mnt/floppy2/home", 3000));   /* on itself */
    CHECK(after(m, "mount /mnt/floppy2/bin /mnt/floppy2/home").find("busy") != std::string::npos);
    REQUIRE(m.command("umount /dev/fdx1", 3000));
    REQUIRE(m.command("ls /tmp", 3000));
    CHECK(after(m, "ls /tmp").find("motd") == std::string::npos);  /* every place of it out */
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
}

}
