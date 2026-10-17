/*
 * test_links.cpp - link and the programs on it: ln, a second name for a
 * file; mv, a name moved - on the same volume by a link, to another by a
 * copy; cp, a file copied.  Each read on the host after.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <fstream>
#include <iterator>
#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

std::string hostFile(const std::string &path)
{
    std::ifstream in(std::string{UNIX_ROOT} + path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("ln: a second name for the same inode, its links counted; rm of one leaves the other") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("echo same > /a"));
    REQUIRE(m.command("ln /a /b"));
    {
        Volume v(m.imageNow());
        const auto a = v.lookup("/a"), b = v.lookup("/b");
        REQUIRE(a);
        REQUIRE(b);
        CHECK(*a == *b);
        CHECK(v.inode(*a).nlink == 2);
    }
    REQUIRE(m.command("rm /a"));
    REQUIRE(m.command("cat /b"));
    CHECK(after(m, "cat /b").find("\nsame\n#") != std::string::npos);
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/a"));
    REQUIRE(v.lookup("/b"));
    CHECK(v.inode(*v.lookup("/b")).nlink == 1);
}

TEST_CASE("ln refused: a name there already, a directory, another volume") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);               /* the root in the RAM disk, /home the diskette's */
    atPrompt(m);
    REQUIRE(m.command("echo x > /a"));
    REQUIRE(m.command("ln /a /etc/motd"));
    REQUIRE(m.command("ln /etc /e"));
    REQUIRE(m.command("ln /a /home/a"));
    INFO(m.screen());
    CHECK(after(m, "ln /a /etc/motd").find("ln: /etc/motd: there already") != std::string::npos);
    CHECK(after(m, "ln /etc /e").find("ln: /etc: a directory") != std::string::npos);
    CHECK(after(m, "ln /a /home/a").find("ln: /home/a: on another volume") != std::string::npos);
}

TEST_CASE("mv: a name moved on its volume, into a directory, and to another volume by a copy") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("echo moving > /a"));
    REQUIRE(m.command("mv /a /b"));
    REQUIRE(m.command("mkdir /d"));
    REQUIRE(m.command("mv /b /d"));
    REQUIRE(m.command("cat /d/b"));
    CHECK(after(m, "cat /d/b").find("\nmoving\n#") != std::string::npos);
    REQUIRE(m.command("mv /d/b /tmp/c", 3000));         /* the RAM disk: copied, then removed */
    REQUIRE(m.command("cat /tmp/c"));
    INFO(m.screen());
    CHECK(after(m, "cat /tmp/c").find("\nmoving\n#") != std::string::npos);
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/a"));
    CHECK_FALSE(v.lookup("/b"));
    CHECK_FALSE(v.lookup("/d/b"));
    REQUIRE(m.command("mv /tmp/none /x"));
    CHECK(after(m, "mv /tmp/none /x").find("mv: /tmp/none: not found") != std::string::npos);
}

TEST_CASE("cp: a file of several blocks copied whole, to a name and into a directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("cp /etc/numbers /n", 3000));
    REQUIRE(m.command("mkdir /d"));
    REQUIRE(m.command("cp /etc/numbers /d", 3000));
    INFO(m.screen());
    Volume v(m.imageNow());
    const auto n = v.lookup("/n"), d = v.lookup("/d/numbers");
    REQUIRE(n);
    REQUIRE(d);
    CHECK(v.content(v.inode(*n)) == hostFile("/etc/numbers"));
    CHECK(v.content(v.inode(*d)) == hostFile("/etc/numbers"));
    CHECK(*n != *v.lookup("/etc/numbers"));              /* a copy, not a link */
}

}
