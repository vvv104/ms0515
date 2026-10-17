/*
 * test_write.cpp - stage 5, writing the file system: files made, written,
 * written over and removed, directories made, by the shell's redirections
 * and by programs; then the diskette read on the host, its files, its
 * directories and its bitmap, by the tests' own reading of the format.
 */
#include "Volume.hpp"

#include <fstream>
#include <iterator>
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

std::string hostFile(const std::string &path)
{
    std::ifstream in(std::string{UNIX_ROOT} + path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The blocks the bitmap has taken. */
int taken(const Volume &v, const std::vector<uint8_t> &image)
{
    const std::size_t at = unixos::blockOffset(static_cast<int>(v.word(1, 14)));
    int n = 0;
    for (int b = 0; b < 1600; ++b) n += (image[at + static_cast<std::size_t>(b / 8)] >> (b % 8)) & 1;
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("a file made by > is there to read, on the machine and on the diskette") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    REQUIRE(m.command("echo hello file > /etc/hi"));
    REQUIRE(m.command("cat /etc/hi"));
    INFO(m.screen());
    CHECK(after(m, "cat /etc/hi").find("\nhello file\n#") != std::string::npos);
    Volume v(m.imageNow());
    const auto hi = v.lookup("/etc/hi");
    REQUIRE(hi);
    CHECK(v.content(v.inode(*hi)) == "hello file\n");
    CHECK((v.inode(*hi).mode & 0170000) == 0100000);
}

TEST_CASE("> over a file writes it anew, its old blocks given back") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    const int before = taken(Volume(m.imageNow()), m.imageNow());
    REQUIRE(m.command("cat /etc/numbers > /etc/hi", 3000));
    const int big = taken(Volume(m.imageNow()), m.imageNow());
    REQUIRE(m.command("echo short > /etc/hi"));
    const auto image = m.imageNow();
    Volume v(image);
    INFO(m.screen());
    CHECK(big == before + 5);                      /* 2120 bytes: five blocks */
    CHECK(taken(v, image) == before + 1);          /* written anew: one */
    CHECK(v.content(v.inode(*v.lookup("/etc/hi"))) == "short\n");
}

TEST_CASE("a file of several blocks is written whole, in one extent, as the host has it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    REQUIRE(m.command("cat /etc/numbers > /copy", 3000));
    Volume v(m.imageNow());
    const auto c = v.lookup("/copy");
    REQUIRE(c);
    CHECK(v.content(v.inode(*c)) == hostFile("/etc/numbers"));
    CHECK(v.inode(*c).nextents == 1);
}

TEST_CASE("mkdir makes a directory with . and .., a file goes in it; rm takes the file away") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    const int before = taken(Volume(m.imageNow()), m.imageNow());
    REQUIRE(m.command("mkdir /work"));
    REQUIRE(m.command("mkdir /work/u"));
    REQUIRE(m.command("echo inside > /work/u/f"));
    REQUIRE(m.command("ls /work/u"));
    INFO(m.screen());
    CHECK(after(m, "ls /work/u").find("\nf\n#") != std::string::npos);
    {
        Volume v(m.imageNow());
        const auto work = v.lookup("/work"), u = v.lookup("/work/u");
        REQUIRE(work);
        REQUIRE(u);
        CHECK((v.inode(*u).mode & 0170000) == 0040000);
        CHECK(v.inode(*work).nlink == 3);         /* its own ., the root's entry, u's .. */
        CHECK(v.lookup("/work/u/..") == work);
        CHECK(v.content(v.inode(*v.lookup("/work/u/f"))) == "inside\n");
    }
    REQUIRE(m.command("rm /work/u/f"));
    REQUIRE(m.command("cat /work/u/f"));
    CHECK(m.screen().find("cat: /work/u/f: not found") != std::string::npos);
    const auto image = m.imageNow();
    Volume v(image);
    CHECK_FALSE(v.lookup("/work/u/f"));
    CHECK(taken(v, image) == before + 2);          /* the two directories' blocks alone */
}

TEST_CASE(">> adds to a file's end - lseek's - and makes one that is not there") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("echo one > /f"));
    REQUIRE(m.command("echo two >> /f"));
    REQUIRE(m.command("cat /etc/numbers >> /f"));
    REQUIRE(m.command("echo three >> /g"));
    INFO(m.screen());
    Volume v(m.imageNow());
    const auto f = v.lookup("/f"), g = v.lookup("/g");
    REQUIRE(f);
    REQUIRE(g);
    CHECK(v.content(v.inode(*f)) == "one\ntwo\n" + hostFile("/etc/numbers"));  /* across blocks */
    CHECK(v.content(v.inode(*g)) == "three\n");
}

TEST_CASE("< gives a command a file for its input") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    REQUIRE(m.command("cat < /etc/motd"));
    INFO(m.screen());
    CHECK(after(m, "cat < /etc/motd").find("Welcome to UNIX") != std::string::npos);
}

TEST_CASE("what was written stays: the diskette booted again has the file") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    std::vector<uint8_t> image;
    {
        Machine m(unixos::readImage());
        atPrompt(m);
        REQUIRE(m.command("echo kept > /kept"));
        image = m.imageNow();
    }
    Machine again(image);
    atPrompt(again);
    REQUIRE(again.command("cat /kept"));
    INFO(again.screen());
    CHECK(after(again, "cat /kept").find("\nkept\n#") != std::string::npos);
}

TEST_CASE("errors: no such directory, a file is no directory, rm of what is not there") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    atPrompt(m);
    REQUIRE(m.command("echo x > /nowhere/f"));
    REQUIRE(m.command("mkdir /etc/motd/d"));
    REQUIRE(m.command("rm /nothing"));
    INFO(m.screen());
    CHECK(m.screen().find("sh: /nowhere/f: not found") != std::string::npos);
    CHECK(m.screen().find("mkdir: /etc/motd/d: not a directory") != std::string::npos);
    CHECK(m.screen().find("rm: /nothing: not found") != std::string::npos);
}

}
