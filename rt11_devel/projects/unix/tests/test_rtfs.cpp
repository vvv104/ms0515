/*
 * test_rtfs.cpp - /bin/rtfs, the files of an RT-11 diskette from the
 * system: ls its directory, get a file off it.  The diskette is made by the
 * emulator's own disk library (src/disk) - an RT-11 volume on side 0 of
 * the second drive, fd2 - and what get took, and put wrote, is read on
 * the host, put's by that library's own reading of RT-11's directory.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <fstream>
#include <iterator>
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
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

std::string hostFile(const std::string &path)
{
    std::ifstream in(std::string{UNIX_ROOT} + path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> bytes(const std::string &s)
{
    return {s.begin(), s.end()};
}

/* A text of n lines, numbered: several blocks for n of some size. */
std::string lines(int n)
{
    std::string s;
    for (int i = 1; i <= n; ++i) s += "rt11 line " + std::to_string(i) + "\n";
    return s;
}

/* An RT-11 diskette, double-sided, side 0 a volume with two files. */
std::vector<uint8_t> rt11Diskette()
{
    auto image = ms0515::disk::blankImage(true);
    ms0515::disk::initVolume(image, 0, true);
    ms0515::disk::putFile(image, 0, true, "HELLO.TXT", bytes("hello from rt-11\n"));
    ms0515::disk::putFile(image, 0, true, "BIG.DAT", bytes(lines(200)));
    return image;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("rtfs ls: an RT-11 diskette's directory, the names and their blocks") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(rt11Diskette());
    atPrompt(m);
    REQUIRE(m.command("rtfs ls /dev/fd2", 3000));
    const std::string out = after(m, "rtfs ls /dev/fd2");
    INFO(m.screen());
    CHECK(out.find("HELLO.TXT") != std::string::npos);
    CHECK(out.find("BIG.DAT") != std::string::npos);
    CHECK(out.find("2 files") != std::string::npos);
}

TEST_CASE("rtfs get: a file off the RT-11 diskette, whole blocks, under a name of the system's") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(rt11Diskette());
    atPrompt(m);
    REQUIRE(m.command("rtfs get /dev/fd2 hello.txt /h", 3000));
    REQUIRE(m.command("rtfs get /dev/fd2 BIG.DAT /b", 3000));
    REQUIRE(m.command("rtfs get /dev/fd2 nope.txt /n", 3000));
    INFO(m.screen());
    CHECK(after(m, "rtfs get /dev/fd2 nope.txt /n").find("rtfs: NOPE.TXT: not found") != std::string::npos);
    Volume v(m.imageNow());
    const auto h = v.lookup("/h"), b = v.lookup("/b");
    REQUIRE(h);
    REQUIRE(b);
    const std::string hs = v.content(v.inode(*h)), bs = v.content(v.inode(*b)), big = lines(200);
    CHECK(hs.size() == 512);                            /* RT-11 keeps blocks, not bytes */
    CHECK(hs.substr(0, 17) == "hello from rt-11\n");
    CHECK(bs.size() == (big.size() + 511) / 512 * 512);
    CHECK(bs.substr(0, big.size()) == big);
    CHECK_FALSE(v.lookup("/n"));
}

TEST_CASE("an RT-11 program from its own diskette, run on the machine: rtfs get, then rt11") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    std::ifstream in(unixos::imagePath().parent_path() / ".." / ".." / ".." / "toolset" / "gcc" / "examples" /
                         "build" / "sav" / "HELLO.SAV",
                     std::ios::binary);
    const std::vector<uint8_t> hello{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (hello.empty()) { MESSAGE("the GCC toolset's HELLO.SAV not built - skipped"); return; }
    auto image = rt11Diskette();
    ms0515::disk::putFile(image, 0, true, "HELLO.SAV", hello);
    Machine m(unixos::readImage(), false);
    m.insertSecond(image);
    atPrompt(m);
    REQUIRE(m.command("rtfs get /dev/fd2 hello.sav /hello.sav", 3000));
    m.typeAndSettle("rt11 /hello.sav\r", 0);
    REQUIRE(m.runUntil("HELLO FROM GCC", 3000));
}

TEST_CASE("rtfs put: files of the system written onto the RT-11 diskette, one over a name there") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(rt11Diskette());
    atPrompt(m);
    REQUIRE(m.command("rtfs put /dev/fd2 /etc/numbers num.txt", 3000));
    REQUIRE(m.command("rtfs put /dev/fd2 /etc/motd hello.txt", 3000));   /* over the old one */
    REQUIRE(m.command("rtfs ls /dev/fd2", 3000));
    INFO(m.screen());
    CHECK(after(m, "rtfs ls /dev/fd2").find("NUM.TXT") != std::string::npos);
    CHECK(after(m, "rtfs ls /dev/fd2").find("3 files") != std::string::npos);
    const auto image = ms0515::disk::openImage(m.secondNow(), 0);
    REQUIRE(image);
    REQUIRE(image->hasDirectory);
    CHECK(image->directory.permanentFiles().size() == 3);
    const std::string numbers = hostFile("/etc/numbers"), motd = hostFile("/etc/motd");
    const auto num = image->readFile("NUM.TXT"), hello = image->readFile("HELLO.TXT"),
               big = image->readFile("BIG.DAT");
    CHECK(num.size() == (numbers.size() + 511) / 512 * 512);
    CHECK(std::string(num.begin(), num.begin() + static_cast<long>(numbers.size())) == numbers);
    CHECK(std::string(hello.begin(), hello.begin() + static_cast<long>(motd.size())) == motd);
    CHECK(std::string(big.begin(), big.begin() + 12) == "rt11 line 1\n");     /* the rest untouched */
}

}
