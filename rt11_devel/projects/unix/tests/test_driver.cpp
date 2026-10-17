/*
 * test_driver.cpp - the diskettes' driver as a file: /boot/fd, loaded by
 * the kernel once the root is mounted, takes the diskettes over from the
 * ROM's own way; with no such file, or a file that is no driver, the ROM's
 * way stays and the system runs as ever.  The driver of the build drives
 * the VG93 itself (drivers/fd.s): once it is in, no block goes through
 * the ROM's 160010.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <ms0515/core/cpu.h>

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

uint16_t driverEntry = 0;
int driverCalls = 0, romCalls = 0;

/* The driver's rw and the ROM's disk entry, counted apart. */
void countDisk(struct ms0515_cpu *cpu)
{
    if (cpu->instruction_pc == driverEntry)
        ++driverCalls;
    else
        ++romCalls;
}

/* The driver as the build made it, beside the images. */
std::vector<uint8_t> driverFile()
{
    std::ifstream in(unixos::imagePath().parent_path() / "fd.bin", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* A word of the driver's header: 0 magic, 1 version, 2 size, 3 init, 4 rw, 5 format. */
unsigned header(const std::vector<uint8_t> &d, int word)
{
    return d[2 * word] | d[2 * word + 1] << 8;
}

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

/* Frames a command takes, five at a time, to its prompt. */
int framesOf(Machine &m, const std::string &command)
{
    m.typeAndSettle(command + "\r", 0);
    int frames = 0;
    while (frames < 6000) {
        m.run(5);
        frames += 5;
        std::string s = m.screen();
        while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
        if (s.size() >= 2 && s.substr(s.size() - 2) == "\n$") break;
    }
    return frames;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the system's /boot/fd drives the VG93 itself: no block through the ROM once it is in") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto d = driverFile();
    REQUIRE(d.size() >= 12);
    CHECK(header(d, 0) == 042106);
    CHECK(header(d, 1) == 2);                 /* the door with format */
    CHECK(header(d, 5) > header(d, 4));
    CHECK(header(d, 2) == d.size());
    CHECK(d.size() <= 1024);
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot(3000));
    {
        INFO(m.screen());
        CHECK(m.screen().find("UNIX: the diskettes by /boot/fd") != std::string::npos);
    }
    atPrompt(m);
    driverEntry = static_cast<uint16_t>(header(d, 4));
    driverCalls = romCalls = 0;
    const std::array<uint16_t, 2> at{driverEntry, 0160010};
    m.emulator().setExecHook(at, countDisk);
    REQUIRE(m.command("cat /etc/numbers"));
    REQUIRE(m.command("echo four > /k"));
    REQUIRE(m.command("cat /k"));
    m.emulator().setExecHook(at, nullptr);
    INFO(m.screen());
    CHECK(driverCalls > 0);
    CHECK(romCalls == 0);
    CHECK(after(m, "cat /k").find("\nfour\n$") != std::string::npos);
    Volume v(m.imageNow());
    const auto k = v.lookup("/k");
    REQUIRE(k);
    CHECK(v.inode(*k).size == 5);
}

TEST_CASE("the driver is faster than the ROM: a file copied in fewer frames") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine own(unixos::readImage(), false), rom(unixos::readBareImage(), false);
    atPrompt(own);
    atPrompt(rom);
    /* Ten blocks read and ten written, with the bitmap and the inode: the
     * disk's work, not the console's. */
    const std::string copy = "cat /bin/mount > /x";
    const int byDriver = framesOf(own, copy), byRom = framesOf(rom, copy);
    MESSAGE(copy << ": the driver " << byDriver << " frames, the ROM " << byRom);
    CHECK(byDriver * 3 < byRom * 2);       /* a third faster at the least; twice it was measured */
}

TEST_CASE("with no /boot/fd the ROM's own way: no word of a driver, the system runs") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    REQUIRE(m.boot(3000));
    CHECK(m.screen().find("/boot/fd") == std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("echo three > /h"));
    REQUIRE(m.command("cat /h"));
    CHECK(after(m, "cat /h").find("\nthree\n$") != std::string::npos);
}

TEST_CASE("a /boot/fd longer than the driver's room is refused, whatever its header says") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    {
        Volume v(image);
        const auto fd = v.lookup("/boot/fd");
        REQUIRE(fd);
        const auto put = [&](int block, int at, unsigned w) {
            const std::size_t o = v.offset(block) + static_cast<std::size_t>(at);
            image[o] = static_cast<uint8_t>(w);
            image[o + 1] = static_cast<uint8_t>(w >> 8);
        };
        /* The file 1100 bytes, three blocks; its header says 1024, the
         * room's worth, which is all a read into the room would bring. */
        const int iblock = static_cast<int>(v.word(1, 16) + (*fd - 1) / 16);
        const int at = static_cast<int>((*fd - 1) % 16 * 32);
        put(iblock, at + 4, 0);
        put(iblock, at + 6, 1100);
        put(iblock, at + 14, 3);
        put(static_cast<int>(v.inode(*fd).start[0]), 4, 1024);
    }
    Machine m(image, false);
    REQUIRE(m.boot(3000));
    INFO(m.screen());
    CHECK(m.screen().find("UNIX: /boot/fd is no driver") != std::string::npos);
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
    REQUIRE(m.boot(3000));
    CHECK(m.screen().find("UNIX: /boot/fd is no driver") != std::string::npos);
    atPrompt(m);
    REQUIRE(m.command("ls /bin"));
    CHECK(after(m, "ls /bin").find("sh") != std::string::npos);
}

}
