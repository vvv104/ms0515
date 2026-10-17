/*
 * test_tools.cpp - the programs that tell about the files: ls -l and -a,
 * du, df.  What they say is checked against the volume read on the host.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>
#include <vector>

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
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

/* ls -l's mode: the type, then rwx three times. */
std::string modeString(unsigned mode)
{
    std::string s = (mode & 0170000) == 0040000 ? "d" : (mode & 0170000) == 0060000 ? "b" : "-";
    for (int shift = 6; shift >= 0; shift -= 3) {
        s += (mode >> shift) & 4 ? 'r' : '-';
        s += (mode >> shift) & 2 ? 'w' : '-';
        s += (mode >> shift) & 1 ? 'x' : '-';
    }
    return s;
}

std::string rightAligned(unsigned long n, std::size_t width)
{
    std::string s = std::to_string(n);
    return std::string(width > s.size() ? width - s.size() : 0, ' ') + s;
}

/* The blocks under the inode: its own, and its entries' when a directory. */
unsigned long blocksUnder(const Volume &v, unsigned ino)
{
    const auto i = v.inode(ino);
    unsigned long n = 0;
    const unsigned type = i.mode & 0170000;
    if (type != 0040000 && type != 0100000) return 0;   /* a device holds none */
    for (unsigned e = 0; e < i.nextents && e < 5; ++e) n += i.len[e];
    if (type == 0040000)
        for (const auto &[name, child] : v.entries(ino))
            if (name != "." && name != "..") n += blocksUnder(v, child);
    return n;
}

/* The blocks the bitmap has taken, of the first `blocks`. */
unsigned taken(const Volume &v, const std::vector<uint8_t> &image, int blocks)
{
    const std::size_t at = v.offset(static_cast<int>(v.word(1, 14)));
    unsigned n = 0;
    for (int b = 0; b < blocks; ++b) n += (image[at + static_cast<std::size_t>(b / 8)] >> (b % 8)) & 1;
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("ls -l: a line a file - its type and permissions, links, size, name; -a shows . and ..") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("ls -l /etc", 3000));
    const std::string out = after(m, "ls -l /etc");
    INFO(m.screen());
    Volume v(m.imageNow());
    for (const char *name : {"motd", "numbers", "fstab"}) {
        const auto ino = v.lookup(std::string("/etc/") + name);
        REQUIRE(ino);
        const auto i = v.inode(*ino);
        const std::string line = modeString(i.mode) + " " + rightAligned(i.nlink, 2) + " " +
                                 rightAligned(i.size, 7) + " " + name;
        CAPTURE(line);
        CHECK(out.find("\n" + line + "\n") != std::string::npos);
    }
    REQUIRE(m.command("ls -a /"));
    CHECK(after(m, "ls -a /").find(".  ..") != std::string::npos);
    REQUIRE(m.command("ls -l /etc/motd"));                 /* a file named: its own line, as named */
    CHECK(after(m, "ls -l /etc/motd").find(" /etc/motd\n$") != std::string::npos);
}

TEST_CASE("du: the blocks under each directory, and -s the total alone") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("mkdir /d"));
    REQUIRE(m.command("cp /etc/numbers /d/n", 3000));
    REQUIRE(m.command("du /d"));
    REQUIRE(m.command("du -s /etc", 3000));
    INFO(m.screen());
    Volume v(m.imageNow());
    const auto d = v.lookup("/d"), etc = v.lookup("/etc");
    REQUIRE(d);
    REQUIRE(etc);
    CHECK(after(m, "du /d").find("\n" + std::to_string(blocksUnder(v, *d)) + " /d\n") != std::string::npos);
    CHECK(after(m, "du -s /etc").find("\n" + std::to_string(blocksUnder(v, *etc)) + " /etc\n$") != std::string::npos);
}

TEST_CASE("df: each volume's blocks, used and free, by its own superblock and bitmap") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);                     /* the RAM disk's volume too */
    atPrompt(m);
    REQUIRE(m.command("df", 6000));
    const std::string out = after(m, "df");
    INFO(m.screen());
    Volume v(m.imageNow());
    const unsigned used = taken(v, m.imageNow(), 1600);
    const std::string root = "/dev/fdx0 " + std::to_string(1600) + " " + std::to_string(used) + " " +
                             std::to_string(1600 - used);
    CAPTURE(root);
    CHECK(out.find(root) != std::string::npos);
    Volume rd(m.ramDisk(), true);
    const unsigned rdUsed = taken(rd, m.ramDisk(), 1024);
    CHECK(out.find("/dev/rd0 1024 " + std::to_string(rdUsed) + " " + std::to_string(1024 - rdUsed)) !=
          std::string::npos);
    CHECK(out.find("/dev/fd0 ") == std::string::npos);        /* the root's side, not a volume of its own */
}

}
