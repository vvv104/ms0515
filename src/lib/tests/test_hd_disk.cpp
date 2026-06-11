/*
 * test_hd_disk.cpp — lib-level regression for the paravirtual HD: device.
 *
 * Drives the HD controller the way HD.SYS does, but through the public
 * Emulator bus (readWord/writeWord against the I/O page) so the whole
 * path is exercised: Emulator::mountHd → file load → board routing →
 * core DMA → flush-on-unmount.  A write transfer is flushed to the
 * backing file on unmount; a fresh Emulator re-reads it back, proving
 * persistence across a mount cycle.
 *
 * This is a device-integration test, not an OS oracle.  Validating the
 * real RT-11 driver (INIT HD: / PIP / DIR HD:) needs a HD.SYS assembled
 * for our V5.04 SJ system and lives in a later end-to-end milestone.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/hd.h>
}

#include <ms0515/Emulator.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

/* Absolute PDP-11 I/O addresses of the HD registers (HDCSR default). */
constexpr uint16_t kDispatcher = 0177400;   /* memory bank dispatcher    */
constexpr uint16_t kHdCsr      = 0177720;   /* command / status          */
constexpr uint16_t kHdData     = 0177722;   /* argument / result         */

/* Issue a command like the driver: argument to HDDAT, then code to HDCSR. */
void hdCmd(ms0515::Emulator &emu, uint16_t arg, uint16_t cmd)
{
    emu.writeWord(kHdData, arg);
    emu.writeWord(kHdCsr, cmd);
}

fs::path makeImage(const char *name, uint32_t blocks)
{
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    std::vector<char> zero(blocks * HD_BLOCK_SIZE, 0);
    f.write(zero.data(), static_cast<std::streamsize>(zero.size()));
    return p;
}

} /* namespace */

TEST_SUITE("HD disk (lib)") {

TEST_CASE("mountHd rejects a non-block-multiple file") {
    fs::path p = fs::temp_directory_path() / "ms0515_hd_bad.img";
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        std::vector<char> junk(777, 0);          /* not a multiple of 512 */
        f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    ms0515::Emulator emu;
    CHECK(emu.mountHd(p.string()) == false);
    CHECK(emu.hdMounted() == false);
    fs::remove(p);
}

TEST_CASE("controller enable is independent of a mounted image") {
    fs::path p = makeImage("ms0515_hd_split.img", 16);
    ms0515::Emulator emu;

    /* Enable the controller with no media — an offline drive. */
    emu.setHdEnabled(true);
    CHECK(emu.hdEnabled());
    CHECK_FALSE(emu.hdMounted());
    hdCmd(emu, 0, HD_CMD_SET_UNIT);
    hdCmd(emu, 0, HD_CMD_GET_SIZE);
    CHECK(emu.readWord(kHdData) == 0);

    /* Mounting brings media in (and keeps the controller enabled). */
    REQUIRE(emu.mountHd(p.string()));
    CHECK(emu.hdEnabled());
    CHECK(emu.hdMounted());
    hdCmd(emu, 0, HD_CMD_GET_SIZE);
    CHECK(emu.readWord(kHdData) == 16);

    /* Ejecting the media leaves the controller present. */
    emu.unmountHd();
    CHECK(emu.hdEnabled());
    CHECK_FALSE(emu.hdMounted());

    /* Switching to the serial port removes the controller. */
    emu.setHdEnabled(false);
    CHECK_FALSE(emu.hdEnabled());

    fs::remove(p);
}

TEST_CASE("mountHd rejects an image past the 65535-block RT-11 limit") {
    fs::path p = fs::temp_directory_path() / "ms0515_hd_huge.img";
    {   /* sparse: one block over the limit, no 32 MB write */
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.seekp(static_cast<std::streamoff>(65536) * HD_BLOCK_SIZE - 1);
        f.put('\0');
    }
    ms0515::Emulator emu;
    CHECK(emu.mountHd(p.string()) == false);
    CHECK_FALSE(emu.hdMounted());
    fs::remove(p);
}

TEST_CASE("mountHd reports size to the driver via GetSize") {
    fs::path p = makeImage("ms0515_hd_size.img", 50);
    ms0515::Emulator emu;
    REQUIRE(emu.mountHd(p.string()));

    hdCmd(emu, 0, HD_CMD_SET_UNIT);
    hdCmd(emu, 0, HD_CMD_GET_SIZE);
    CHECK(emu.readWord(kHdData) == 50);

    emu.unmountHd();
    fs::remove(p);
}

TEST_CASE("write transfer persists to the backing file across a remount") {
    fs::path p = makeImage("ms0515_hd_rt.img", 8);

    const uint16_t buf = 0x2000;                 /* RAM under primary banks */
    const uint16_t block = 3;
    const uint32_t fileOff = block * HD_BLOCK_SIZE;
    std::vector<uint16_t> pattern = {0xC000, 0xC001, 0xC002, 0xC003};

    {
        ms0515::Emulator emu;
        REQUIRE(emu.mountHd(p.string()));
        CHECK(emu.hdMounted());

        emu.writeWord(kDispatcher, 0x007F);      /* map all primary banks */
        for (size_t w = 0; w < pattern.size(); ++w)
            emu.writeWord(static_cast<uint16_t>(buf + w * 2), pattern[w]);

        hdCmd(emu, block, HD_CMD_SET_BLOCK);
        hdCmd(emu, buf, HD_CMD_SET_BUF);
        hdCmd(emu, static_cast<uint16_t>(pattern.size()), HD_CMD_SET_WCNT);
        hdCmd(emu, 0, HD_CMD_WRITE);

        CHECK((emu.readWord(kHdCsr) & HD_CS_ERROR) == 0);
        CHECK(emu.hdActive());                   /* lamp lit after transfer */

        /* Write-through: the bytes are already in the file mid-session,
         * before any unmount (this is the whole point — no data lives only
         * in RAM waiting for a clean shutdown). */
        {
            std::ifstream f(p, std::ios::binary);
            f.seekg(fileOff);
            for (uint16_t want : pattern) {
                uint8_t lo = 0, hi = 0;
                f.read(reinterpret_cast<char *>(&lo), 1);
                f.read(reinterpret_cast<char *>(&hi), 1);
                CHECK((uint16_t)(lo | (hi << 8)) == want);
            }
        }

        emu.unmountHd();
        CHECK(emu.hdMounted() == false);
    }

    /* A fresh Emulator reads the same data back over the bus. */
    {
        ms0515::Emulator emu;
        REQUIRE(emu.mountHd(p.string()));
        emu.writeWord(kDispatcher, 0x007F);

        const uint16_t rbuf = 0x3000;
        hdCmd(emu, block, HD_CMD_SET_BLOCK);
        hdCmd(emu, rbuf, HD_CMD_SET_BUF);
        hdCmd(emu, static_cast<uint16_t>(pattern.size()), HD_CMD_SET_WCNT);
        hdCmd(emu, 0, HD_CMD_READ);

        CHECK((emu.readWord(kHdCsr) & HD_CS_ERROR) == 0);
        for (size_t w = 0; w < pattern.size(); ++w)
            CHECK(emu.readWord(static_cast<uint16_t>(rbuf + w * 2)) == pattern[w]);

        emu.unmountHd();
    }

    fs::remove(p);
}

TEST_CASE("a .rtfs descriptor mounts a folder-backed HD volume") {
    fs::path dir = fs::temp_directory_path() / "ms0515_rtfs_mount";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream(dir / "hello.dat", std::ios::binary)
            << std::string(16, '!');
        std::ofstream(dir / "device.rtfs", std::ios::binary)
            << "device: hd\nblocks: 200\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.mountHd((dir / "device.rtfs").string()));
    CHECK(emu.hdMounted());
    emu.writeWord(kDispatcher, 0x007F);

    /* GetSize reports the descriptor's block count. */
    hdCmd(emu, 0, HD_CMD_SET_UNIT);
    hdCmd(emu, 0, HD_CMD_GET_SIZE);
    CHECK(emu.readWord(kHdData) == 200);

    /* DMA-read the file's first data block through the device. */
    const uint16_t buf = 0x2000;
    hdCmd(emu, 14, HD_CMD_SET_BLOCK);            /* first data block */
    hdCmd(emu, buf, HD_CMD_SET_BUF);
    hdCmd(emu, 8, HD_CMD_SET_WCNT);
    hdCmd(emu, 0, HD_CMD_READ);
    CHECK((emu.readWord(kHdCsr) & HD_CS_ERROR) == 0);
    CHECK(emu.readWord(buf) == (uint16_t)('!' | ('!' << 8)));

    /* DMA-write lands in the host file. */
    emu.writeWord(buf, (uint16_t)('A' | ('B' << 8)));
    hdCmd(emu, 14, HD_CMD_SET_BLOCK);
    hdCmd(emu, buf, HD_CMD_SET_BUF);
    hdCmd(emu, 1, HD_CMD_SET_WCNT);
    hdCmd(emu, 0, HD_CMD_WRITE);
    CHECK((emu.readWord(kHdCsr) & HD_CS_ERROR) == 0);
    {
        std::ifstream f(dir / "hello.dat", std::ios::binary);
        char two[2]{};
        f.read(two, 2);
        CHECK(two[0] == 'A');
        CHECK(two[1] == 'B');
    }

    /* A floppy descriptor must NOT mount into the HD slot — and the same
     * descriptor DOES mount on a floppy unit (and vice versa). */
    {
        std::ofstream(dir / "fd.rtfs", std::ios::binary)
            << "device: floppy\nblocks: 800\n";
    }
    ms0515::Emulator emu2;
    CHECK_FALSE(emu2.mountHd((dir / "fd.rtfs").string()));
    CHECK(emu2.mountDisk(1, (dir / "fd.rtfs").string()));
    CHECK(emu2.diskPath(1) == (dir / "fd.rtfs").string());
    CHECK_FALSE(emu2.mountDisk(0, (dir / "device.rtfs").string()));
    emu2.unmountDisk(1);

    emu.unmountHd();
    fs::remove_all(dir);
}

} /* TEST_SUITE("HD disk (lib)") */
