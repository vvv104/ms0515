/*
 * test_ramdisk.cpp - the RAM disk's driver as a file: /boot/rd, loaded by
 * the kernel once the root is mounted, as /boot/fd is (test_driver.cpp).
 * Its init finds the EX board and tells its blocks; with no board, no
 * file, or a file that is no driver, there is no RAM disk and the system
 * runs on the diskette.  The kernel itself has no word of the board.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

std::vector<uint8_t> buildFile(const char *name)
{
    std::ifstream in(unixos::imagePath().parent_path() / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

unsigned word(const std::vector<uint8_t> &d, std::size_t at)
{
    return d[at] | d[at + 1] << 8;
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("/boot/rd is a driver: its header, and it fits its room beside /boot/fd") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto rd = buildFile("rd.bin"), fd = buildFile("fd.bin");
    REQUIRE(rd.size() >= 10);
    CHECK(word(rd, 0) == 042122);             /* "RD" */
    CHECK(word(rd, 2) == 1);
    CHECK(word(rd, 4) == rd.size());
    CHECK(rd.size() <= 0500);                 /* 156300..157000 */
    CHECK(fd.size() <= 01300);                /* 155000..156300 */
    for (const auto &image : {unixos::readImage(), unixos::readBareImage()}) {
        Volume v(image);
        const auto at = v.lookup("/boot/rd");
        REQUIRE(at);
        CHECK(v.content(v.inode(*at)) == std::string(rd.begin(), rd.end()));
    }
}

TEST_CASE("the kernel has no word of the EX board: its ports are the driver's alone") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto kernel = buildFile("kernel.bin"), rd = buildFile("rd.bin");
    /* A port's word, not an offset after an instruction whose operand is
     * PC-relative (mode 67: a call's or a branch's distance is a word like
     * any other, and two of the kernel's have been 177536 and 177532). */
    const auto holds = [](const std::vector<uint8_t> &d, unsigned w) {
        for (std::size_t i = 2; i + 1 < d.size(); i += 2) {
            const unsigned before = word(d, i - 2);
            if (word(d, i) == w && (before & 077) != 067 && (before >> 6 & 077) != 067) return true;
        }
        return false;
    };
    for (unsigned port : {0177530u, 0177532u, 0177536u, 0177550u}) {
        CAPTURE(port);
        CHECK_FALSE(holds(kernel, port));
        CHECK(holds(rd, port));
    }
}

TEST_CASE("a /boot/rd that is no driver is said so: no RAM disk, the system runs") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    {
        Volume v(image);
        const auto rd = v.lookup("/boot/rd");
        REQUIRE(rd);
        image[v.offset(static_cast<int>(v.inode(*rd).start[0]))] ^= 0xFF;   /* the magic broken */
    }
    Machine m(image, true);
    REQUIRE(m.boot(3000));
    REQUIRE(m.command("ls /bin"));
    INFO(m.screen());
    CHECK(m.screen().find("kernel: /boot/rd is no driver") != std::string::npos);
    CHECK(m.screen().find("no RAM disk") != std::string::npos);
    CHECK(m.screen().find("mount: /dev/rd0 on /tmp") == std::string::npos);
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

TEST_CASE("the RAM disk by /boot/rd: a file written on it and read back") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot(3000));
    REQUIRE(m.command("cat /etc/numbers > /tmp/n", 3000));        /* the root is the RAM disk's */
    REQUIRE(m.command("cat /tmp/n | cat > /home/n", 4000));       /* the diskette's */
    INFO(m.screen());
    Volume v(m.imageNow());
    const auto numbers = v.lookup("/etc/numbers"), copy = v.lookup("/home/n");
    REQUIRE(numbers);
    REQUIRE(copy);
    CHECK(v.content(v.inode(*copy)) == v.content(v.inode(*numbers)));
    Volume rd(m.ramDisk(), true);
    const auto n = rd.lookup("/tmp/n");
    REQUIRE(n);
    CHECK(rd.content(rd.inode(*n)) == v.content(v.inode(*numbers)));
}

}
