#include <doctest/doctest.h>

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Layout.hpp>
#include <ms0515/disk/SparseVolume.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace ms0515::disk;

namespace {

std::vector<uint8_t> blockOf(uint8_t value)
{
    return std::vector<uint8_t>(kBlock, value);
}

std::vector<uint8_t> read(const SparseVolume &vol, int lbn)
{
    std::vector<uint8_t> out(kBlock, 0xEE);
    vol.readBlock(lbn, out.data());
    return out;
}

}  /* namespace */

TEST_SUITE("SparseVolume") {

TEST_CASE("a volume holds what was written and nothing else") {
    SparseVolume vol(800);
    CHECK(vol.blocks() == 800);
    CHECK(vol.held() == 0);
    CHECK(read(vol, 5) == blockOf(0));

    vol.writeBlock(5, blockOf(0xAB).data());
    vol.writeBlock(700, blockOf(0xCD).data());
    CHECK(vol.held() == 2);
    CHECK(vol.holds(5));
    CHECK_FALSE(vol.holds(6));
    CHECK(read(vol, 5) == blockOf(0xAB));
    CHECK(read(vol, 700) == blockOf(0xCD));
    CHECK(read(vol, 6) == blockOf(0));

    /* Zeros written over a block let it go; so does clearing it. */
    vol.writeBlock(5, blockOf(0).data());
    CHECK(vol.held() == 1);
    CHECK(read(vol, 5) == blockOf(0));
    vol.clearBlock(700);
    CHECK(vol.held() == 0);

    /* Past the end: nothing is written, zeros are read. */
    vol.writeBlock(800, blockOf(1).data());
    vol.writeBlock(-1, blockOf(1).data());
    CHECK(vol.held() == 0);
    CHECK(read(vol, 800) == blockOf(0));
    CHECK(read(vol, -1) == blockOf(0));
}

TEST_CASE("the size can be raised, and lowered at the cost of what lay past it") {
    SparseVolume vol(10);
    vol.writeBlock(9, blockOf(9).data());
    vol.resize(1000);
    CHECK(vol.blocks() == 1000);
    vol.writeBlock(999, blockOf(7).data());
    CHECK(read(vol, 9) == blockOf(9));
    CHECK(read(vol, 999) == blockOf(7));

    vol.resize(500);
    CHECK(vol.held() == 1);
    CHECK(read(vol, 999) == blockOf(0));
    CHECK(read(vol, 9) == blockOf(9));
}

TEST_CASE("the serial form comes back the same volume, and is as large as its content") {
    SparseVolume vol(65535);
    vol.writeBlock(0, blockOf(1).data());
    vol.writeBlock(6, blockOf(2).data());
    vol.writeBlock(65534, blockOf(3).data());

    const auto bytes = vol.serialize();
    CHECK(bytes.size() == 20 + 3 * (4 + 512));
    CHECK(std::string(bytes.begin(), bytes.begin() + 8) == "MS0515SV");

    std::string error;
    const auto back = SparseVolume::parse(bytes, &error);
    REQUIRE_MESSAGE(back.has_value(), error);
    CHECK(back->blocks() == 65535);
    CHECK(back->held() == 3);
    CHECK(read(*back, 0) == blockOf(1));
    CHECK(read(*back, 6) == blockOf(2));
    CHECK(read(*back, 65534) == blockOf(3));
    CHECK(back->serialize() == bytes);

    /* An empty one is its header. */
    CHECK(SparseVolume(800).serialize().size() == 20);
}

TEST_CASE("what is not a volume is refused, with the reason") {
    SparseVolume vol(100);
    vol.writeBlock(3, blockOf(3).data());
    vol.writeBlock(50, blockOf(5).data());
    const auto good = vol.serialize();

    const auto refused = [](std::vector<uint8_t> bytes) {
        std::string error;
        const bool ok = SparseVolume::parse(bytes, &error).has_value();
        CHECK(ok == error.empty());
        return !ok;
    };
    CHECK(refused({}));
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 1)));     /* cut short */
    auto bad = good; bad[0] = 'X';                  CHECK(refused(bad));    /* the magic */
    bad = good; bad[8] = 2;                         CHECK(refused(bad));    /* a version unknown */
    bad = good; bad[11] = 4;                        CHECK(refused(bad));    /* the block size */
    bad = good; bad[20] = 200;                      CHECK(refused(bad));    /* a block past the end */
    bad = good; bad[20] = 60;                       CHECK(refused(bad));    /* not in rising order */
    bad = good; bad[16] = 3;                        CHECK(refused(bad));    /* more blocks than there are */
    CHECK_FALSE(refused(good));
}

TEST_CASE("an RT-11 volume goes into a sparse one and back, block for block") {
    auto linear = blankLinear(200);
    initVolume(linear, 0, false, {}, Vol::linear);
    putFile(linear, 0, false, "A.DAT", std::vector<uint8_t>(3 * kBlock, 0x11), {}, Vol::linear);
    putFile(linear, 0, false, "B.DAT", std::vector<uint8_t>(700, 0x22), {}, Vol::linear);

    const auto vol = SparseVolume::fromLinear(linear);
    CHECK(vol.blocks() == 200);
    CHECK(vol.held() < 20);
    CHECK(vol.toLinear() == linear);

    /* The tools read it as they read any linear volume. */
    const auto image = openLinearImage(vol.toLinear());
    REQUIRE(image.has_value());
    CHECK(image->directory.find("A.DAT") != nullptr);
    CHECK(image->readFile("B.DAT")[0] == 0x22);
}

}
