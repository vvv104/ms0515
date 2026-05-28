/*
 * test_build.cpp — create / init / put primitives round-trip through the
 * parser, plus error handling.  Files written must read back with identical
 * names, lengths and (block-padded) bytes, single- and double-sided.  The
 * "does a real OS agree" checks live in the lib tests.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ms0515::disk;

namespace {

struct BuildFile { std::string name; std::vector<uint8_t> data; };

std::vector<uint8_t> pattern(std::size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    uint32_t s = seed * 2654435761u + 1;
    for (auto &b : v) { s = s * 1103515245u + 12345u; b = static_cast<uint8_t>(s >> 16); }
    return v;
}

std::vector<BuildFile> diverseFiles()
{
    return {
        {"TEXT.TXT",  std::vector<uint8_t>{'H','e','l','l','o',' ','M','S','0','5','1','5','\r','\n'}},
        {"ZEROS.DAT", std::vector<uint8_t>(700, 0x00)},   /* spans 2 blocks, padded */
        {"FF.DAT",    std::vector<uint8_t>(512, 0xFF)},    /* exactly 1 block */
        {"RND.BIN",   pattern(1500, 42)},                 /* 3 blocks */
        {"ONE.B",     std::vector<uint8_t>{0xAB}},         /* 1 byte -> 1 block */
    };
}

std::vector<uint8_t> makeVolume(bool ds, int side, const std::vector<BuildFile> &files)
{
    auto img = blankImage(ds);
    if (ds) { initVolume(img, 0, true); initVolume(img, 1, true); }
    else      initVolume(img, 0, false);
    for (const auto &f : files) putFile(img, side, ds, f.name, f.data);
    return img;
}

void verifyFiles(const Image &img, const std::vector<BuildFile> &files)
{
    REQUIRE(img.hasDirectory);
    REQUIRE(img.directory.permanentFiles().size() == files.size());
    for (const auto &f : files) {
        const DirEntry *e = img.directory.find(f.name);
        REQUIRE_MESSAGE(e != nullptr, "missing " << f.name);
        const int wantBlocks = static_cast<int>((f.data.size() + kBlock - 1) / kBlock);
        CHECK(e->length == wantBlocks);

        auto got = img.readFile(f.name);
        REQUIRE(got.size() == static_cast<std::size_t>(wantBlocks) * kBlock);
        CHECK_MESSAGE(std::equal(f.data.begin(), f.data.end(), got.begin()),
                      "content mismatch for " << f.name);
        for (std::size_t i = f.data.size(); i < got.size(); ++i)
            CHECK(got[i] == 0);
    }
}

}  /* namespace */

TEST_SUITE("Build") {

TEST_CASE("single-sided init + put round-trip") {
    const auto files = diverseFiles();
    auto image = makeVolume(false, 0, files);
    REQUIRE(image.size() == kSideSize);
    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    verifyFiles(*img, files);
}

TEST_CASE("double-sided: write + read back both sides byte-exact") {
    const auto side0 = diverseFiles();
    const std::vector<BuildFile> side1 = {
        {"MAGIC.DAT", pattern(900, 7)},
        {"BOOT2.SYS", std::vector<uint8_t>(2048, 0x5A)},
    };
    auto img = blankImage(true);
    initVolume(img, 0, true);
    initVolume(img, 1, true);
    for (const auto &f : side0) putFile(img, 0, true, f.name, f.data);
    for (const auto &f : side1) putFile(img, 1, true, f.name, f.data);
    REQUIRE(img.size() == kDoubleSize);

    auto s0 = openImage(img, 0);
    auto s1 = openImage(img, 1);
    REQUIRE(s0.has_value());
    REQUIRE(s1.has_value());
    verifyFiles(*s0, side0);
    verifyFiles(*s1, side1);
}

TEST_CASE("init leaves free sectors as the B6 6D blank pattern") {
    auto image = makeVolume(false, 0, diverseFiles());
    const std::size_t off = lbnToByte(300, 0, false);   /* well past the files */
    CHECK(image[off + 0] == 0xB6);
    CHECK(image[off + 1] == 0x6D);
    CHECK(image[off + 2] == 0xB6);
    CHECK(image[off + 3] == 0x6D);
}

TEST_CASE("init options: volume id and segment count") {
    auto img = blankImage(false);
    InitOptions opts; opts.volumeId = "MYDISK"; opts.owner = "VVV"; opts.segments = 2;
    initVolume(img, 0, false, opts);

    const std::size_t home = lbnToByte(1, 0, false);
    CHECK(std::string(reinterpret_cast<const char *>(&img[home + 0x1D8]), 6) == "MYDISK");
    CHECK(std::string(reinterpret_cast<const char *>(&img[home + 0x1E4]), 3) == "VVV");

    const std::size_t seg = lbnToByte(6, 0, false);
    CHECK(img[seg + 0] == 2);     /* segTotal */
    CHECK(img[seg + 8] == 10);    /* data start = 6 + 2*segments */

    putFile(img, 0, false, "X.DAT", std::vector<uint8_t>(10, 1));
    auto im = openImage(img, 0);
    REQUIRE(im.has_value());
    const DirEntry *e = im->directory.find("X.DAT");
    REQUIRE(e != nullptr);
    CHECK(e->startBlock == 10);
}

TEST_CASE("split / merge round-trip is the identity") {
    /* Build a DS image with distinct files on each side, split it, and
     * confirm each side is the standalone SS volume; merge back == original. */
    const auto s0files = diverseFiles();
    const std::vector<BuildFile> s1files = {{"MAGIC.DAT", pattern(900, 7)}};
    auto ds = blankImage(true);
    initVolume(ds, 0, true); initVolume(ds, 1, true);
    for (const auto &f : s0files) putFile(ds, 0, true, f.name, f.data);
    for (const auto &f : s1files) putFile(ds, 1, true, f.name, f.data);

    auto sides = splitDoubleSided(ds);
    REQUIRE(sides.has_value());
    REQUIRE(sides->first.size()  == kSideSize);
    REQUIRE(sides->second.size() == kSideSize);

    /* Each split side reads as a standalone single-sided volume. */
    auto a = openImage(sides->first, 0);
    auto b = openImage(sides->second, 0);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    verifyFiles(*a, s0files);
    verifyFiles(*b, s1files);

    /* Merge restores the exact original DS bytes. */
    auto back = mergeSides(sides->first, sides->second);
    REQUIRE(back.has_value());
    CHECK(*back == ds);

    CHECK_FALSE(splitDoubleSided(blankImage(false)).has_value());   /* wrong size */
    CHECK_FALSE(mergeSides(ds, ds).has_value());                    /* wrong size */
}

TEST_CASE("putFile errors") {
    auto img = blankImage(false);
    initVolume(img, 0, false);
    const std::vector<uint8_t> d{1, 2, 3};

    SUBCASE("does not fit") {
        std::vector<uint8_t> big(800 * kBlock, 0);   /* bigger than the volume */
        CHECK_THROWS_AS(putFile(img, 0, false, "BIG.DAT", big), std::runtime_error);
    }
    SUBCASE("name longer than 6.3") {
        CHECK_THROWS_AS(putFile(img, 0, false, "TOOLONGNM.TXT", d), std::runtime_error);
    }
    SUBCASE("illegal (non-RAD50) character in name") {
        CHECK_THROWS_AS(putFile(img, 0, false, "BAD-NM.TXT", d), std::runtime_error);
    }
    SUBCASE("not initialised") {
        auto raw = blankImage(false);   /* never init'd */
        CHECK_THROWS_AS(putFile(raw, 0, false, "X.DAT", d), std::runtime_error);
    }
}

} /* TEST_SUITE */
