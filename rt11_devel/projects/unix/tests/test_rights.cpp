/*
 * test_rights.cpp - a file's rights: one rwx, there being no users, kept in
 * its inode.  r to read it, w to write it, empty it or remove it, x to run
 * it; the system's files are r-x, so they are neither removed nor written
 * over, the rest rw-.  A new file is rw-, chmod changes it.  A directory's rights are not looked at.
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

unsigned rights(const Volume &v, const char *path)
{
    const auto ino = v.lookup(path);
    REQUIRE(ino);
    return v.inode(*ino).mode & 07777;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the rights laid down: r-x the system's, rw- the rest; a new file rw-") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo x > /home/f"));
    REQUIRE(m.command("mkdir /home/d"));
    const auto image = m.imageNow();
    Volume v(image);
    for (const char *path : {"/boot/kernel", "/boot/fd", "/bin/sh", "/bin/ls", "/etc/init"}) {
        CAPTURE(path);
        CHECK(rights(v, path) == 5);
    }
    for (const char *path : {"/etc/motd", "/etc/rc", "/etc/fstab", "/dev/fd0", "/home/f"}) {
        CAPTURE(path);
        CHECK(rights(v, path) == 6);
    }
    for (const char *path : {"/", "/bin", "/home", "/home/d"}) {
        CAPTURE(path);
        CHECK(rights(v, path) == 7);
    }
    Volume rd(m.ramDisk(), true);                              /* made by mkfs on the machine */
    CHECK(rights(rd, "/") == 7);
    REQUIRE(m.command("chmod 0 /home/f"));
    REQUIRE(m.command("ls -l /bin/sh /etc/motd /home/f"));
    const std::string out = after(m, "ls -l /bin/sh /etc/motd /home/f");
    INFO(m.screen());
    CHECK(out.find("\n-r-x ") != std::string::npos);
    CHECK(out.find("\n-rw- ") != std::string::npos);
    CHECK(out.find("\n---- ") != std::string::npos);
}

TEST_CASE("a system file is neither removed nor written over, and still runs") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("rm /bin/ls"));
    REQUIRE(m.command("echo x > /bin/ls"));
    REQUIRE(m.command("cp /etc/motd /bin/ls"));
    REQUIRE(m.command("mv /bin/ls /home/ls"));
    REQUIRE(m.command("ls /etc"));
    INFO(m.screen());
    CHECK(after(m, "rm /bin/ls").find("rm: /bin/ls: not allowed") != std::string::npos);
    CHECK(after(m, "echo x > /bin/ls").find("sh: /bin/ls: not allowed") != std::string::npos);
    CHECK(after(m, "cp /etc/motd /bin/ls").find("/bin/ls: not allowed") != std::string::npos);
    CHECK(after(m, "mv /bin/ls /home/ls").find("/bin/ls: not allowed") != std::string::npos);
    CHECK(after(m, "ls /etc").find("motd") != std::string::npos);
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/home/ls"));                         /* mv left no second name */
}

TEST_CASE("chmod: x lets a file run, no w keeps it, no r shuts it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("cp /bin/hello h", 3000));
    REQUIRE(m.command("/home/h"));
    CHECK(after(m, "/home/h").find("/home/h: not allowed") != std::string::npos);
    REQUIRE(m.command("chmod r-x h"));
    REQUIRE(m.command("/home/h"));
    CHECK(after(m, "/home/h").find("hello, world") != std::string::npos);
    REQUIRE(m.command("ls -l h"));
    CHECK(after(m, "ls -l h").find("-r-x ") != std::string::npos);
    REQUIRE(m.command("rm h"));
    CHECK(after(m, "rm h").find("rm: h: not allowed") != std::string::npos);
    REQUIRE(m.command("echo text > t"));
    REQUIRE(m.command("chmod 0 t"));
    REQUIRE(m.command("cat t"));
    CHECK(after(m, "cat t").find("cat: t: not allowed") != std::string::npos);
    REQUIRE(m.command("chmod 6 t"));
    REQUIRE(m.command("cat t"));
    CHECK(after(m, "cat t").find("\ntext\n#") != std::string::npos);
    REQUIRE(m.command("chmod rwz t"));
    INFO(m.screen());
    CHECK(after(m, "chmod rwz t").find("chmod:") != std::string::npos);
    REQUIRE(m.command("chmod rw- h"));
    REQUIRE(m.command("rm h"));
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/home/h"));
}

}
