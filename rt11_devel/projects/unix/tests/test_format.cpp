/*
 * test_format.cpp - fdformat: a diskette's tracks written anew, a program
 * of its own that builds each track's image and hands it to the kernel,
 * which gives it to /boot/fd's WRITE TRACK.  Read on the host: every sector
 * of what was formatted holds the filler, E5, and nothing else is touched;
 * the side takes a volume after.  Without the driver, or on a volume in use,
 * fdformat is refused.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <cstdint>
#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

constexpr std::size_t kImage = 819200, kSide = 5120, kCylinder = 2 * kSide;
constexpr uint8_t kOld = 0x55, kFiller = 0xE5;

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

/* The bytes of a side of the image other than `value`. */
std::size_t otherThan(const std::vector<uint8_t> &image, int side, uint8_t value)
{
    std::size_t n = 0;
    for (std::size_t c = 0; c < 80; ++c)
        for (std::size_t i = 0; i < kSide; ++i)
            n += image[c * kCylinder + static_cast<std::size_t>(side) * kSide + i] != value;
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("fdformat writes a side's 80 tracks anew; the other side untouched; mkfs and mount take it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(std::vector<uint8_t>(kImage, kOld));
    atPrompt(m);
    REQUIRE(m.command("fdformat /dev/fd2", 6000));
    INFO(m.screen());
    CHECK(after(m, "fdformat /dev/fd2").find("fdformat: /dev/fd2: 80 tracks") != std::string::npos);
    const auto image = m.secondNow();
    REQUIRE(image.size() == kImage);
    CHECK(otherThan(image, 0, kFiller) == 0);
    CHECK(otherThan(image, 1, kOld) == 0);
    REQUIRE(m.command("mkfs /dev/fd2", 3000));
    REQUIRE(m.command("mount /dev/fd2 /tmp"));
    REQUIRE(m.command("echo new > /tmp/n"));
    REQUIRE(m.command("cat /tmp/n"));
    CHECK(after(m, "cat /tmp/n").find("\nnew\n#") != std::string::npos);
    Volume v(m.secondNow());
    CHECK(v.lookup("/n"));
}

TEST_CASE("fdformat on fdx1 writes both sides of the second drive, 160 tracks") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    m.insertSecond(std::vector<uint8_t>(kImage, kOld));
    atPrompt(m);
    REQUIRE(m.command("fdformat /dev/fdx1", 12000));
    INFO(m.screen());
    CHECK(after(m, "fdformat /dev/fdx1").find("fdformat: /dev/fdx1: 160 tracks") != std::string::npos);
    const auto image = m.secondNow();
    CHECK(otherThan(image, 0, kFiller) == 0);
    CHECK(otherThan(image, 1, kFiller) == 0);
}

TEST_CASE("fdformat is refused on a volume in use, and without /boot/fd") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    {
        Machine m(unixos::readImage(), false);
        atPrompt(m);
        REQUIRE(m.command("fdformat /dev/fd1"));          /* a side of the root, fdx0 */
        INFO(m.screen());
        CHECK(after(m, "fdformat /dev/fd1").find("fdformat: /dev/fd1: busy") != std::string::npos);
        REQUIRE(m.command("ls /bin"));
        CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
    }
    {
        Machine m(unixos::readBareImage(), false);
        m.insertSecond(std::vector<uint8_t>(kImage, kOld));
        atPrompt(m);
        REQUIRE(m.command("fdformat /dev/fd2"));
        INFO(m.screen());
        CHECK(after(m, "fdformat /dev/fd2").find("fdformat: /dev/fd2: not without /boot/fd") != std::string::npos);
        CHECK(otherThan(m.secondNow(), 0, kOld) == 0);
    }
}

}
