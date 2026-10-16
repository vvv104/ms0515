/*
 * test_driver.cpp - the diskettes' driver as a file: /boot/fd, loaded by
 * the kernel once the root is mounted, takes the diskettes over from the
 * ROM's own way; with no such file, or a file that is no driver, the ROM's
 * way stays and the system runs as ever.  The driver of the build is the
 * ROM's 160010 behind the driver's door (drivers/romfd.s), the door tried
 * before a driver of its own comes through it.
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

int driverCalls = 0;
void countDriver(struct ms0515_cpu *) { ++driverCalls; }

/* The driver as the build made it, beside the images. */
std::vector<uint8_t> driverFile()
{
    std::ifstream in(unixos::imagePath().parent_path() / "fd.bin", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* A word of the driver's header: 0 magic, 1 version, 2 size, 3 init, 4 rw. */
unsigned header(const std::vector<uint8_t> &d, int word)
{
    return d[2 * word] | d[2 * word + 1] << 8;
}

void atPrompt(Machine &m)
{
    REQUIRE(m.runUntil("login:", 3000));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.waitPrompt());
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the system's /boot/fd takes the diskettes over: its entry is where the blocks go") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto d = driverFile();
    REQUIRE(d.size() >= 10);
    CHECK(header(d, 0) == 042106);
    CHECK(header(d, 1) == 1);
    CHECK(header(d, 2) == d.size());
    Machine m(unixos::readImage(), false);
    REQUIRE(m.runUntil("login:", 3000));
    {
        INFO(m.screen());
        CHECK(m.screen().find("UNIX: the diskettes by /boot/fd") != std::string::npos);
    }
    atPrompt(m);
    driverCalls = 0;
    m.emulator().setExecHook(static_cast<uint16_t>(header(d, 4)), countDriver);
    REQUIRE(m.command("cat /etc/numbers"));
    m.emulator().setExecHook(static_cast<uint16_t>(header(d, 4)), nullptr);
    INFO(m.screen());
    CHECK(driverCalls > 0);
    CHECK(m.screen().find("line 40: the quick brown fox") != std::string::npos);
}

TEST_CASE("with no /boot/fd the ROM's own way: no word of a driver, the system runs") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    REQUIRE(m.runUntil("login:", 3000));
    CHECK(m.screen().find("/boot/fd") == std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("echo three > /h"));
    REQUIRE(m.command("cat /h"));
    CHECK(after(m, "cat /h").find("\nthree\n$") != std::string::npos);
}

TEST_CASE("a /boot/fd that is no driver is said so, and the ROM's way stays") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    {
        Volume v(image);
        const auto fd = v.lookup("/boot/fd");
        REQUIRE(fd);
        image[v.offset(static_cast<int>(v.inode(*fd).start[0]))] ^= 0xFF;   /* the magic broken */
    }
    Machine m(image, false);
    REQUIRE(m.runUntil("login:", 3000));
    CHECK(m.screen().find("UNIX: /boot/fd is no driver") != std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("ls /bin"));
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

}
