/*
 * test_boot.cpp - stage 1: the native ROM-B boots block 0, the boot block
 * loads the kernel the superblock names, the kernel prints through the
 * ROM's console.
 */
#include "Machine.hpp"

using unixos::Machine;

TEST_SUITE("UNIX") {

TEST_CASE("the ROM boots the boot block, the boot block the kernel, the kernel the shell") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    const bool shown = m.boot(1500);
    INFO(m.screen());
    CHECK(shown);
    CHECK_FALSE(m.emulator().halted());
}

TEST_CASE("a superblock without the system's magic: the boot block does not jump into a kernel") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    auto image = unixos::readImage();
    REQUIRE(image.size() == 819200);
    const std::size_t at = unixos::blockOffset(1);
    image[at] = static_cast<uint8_t>(image[at] ^ 0xFF);       /* the magic spoilt */
    Machine m(image);
    m.run(1500);
    INFO(m.screen());
    CHECK(m.screen().find("UNIX:") == std::string::npos);
}

TEST_CASE("the image is a double-sided diskette, block 0 a boot block the ROM takes") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto image = unixos::readImage();
    REQUIRE(image.size() == 819200);
    const std::size_t at = unixos::blockOffset(0);
    CHECK(at == 10240);                                       /* track 1, sector 1 */
    CHECK(image[at] == 0240);                                 /* NOP, as a boot block begins */
    CHECK(image[at + 1] == 0);
}

}
