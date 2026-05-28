/*
 * test_build.cpp — buildVolume / buildDoubleSided round-trip through the
 * parser.  Files written must read back with identical names, lengths and
 * (block-padded) bytes, for single- and double-sided images.  This catches
 * writer bugs; the "does a real OS agree" check lives in the lib tests.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

using namespace ms0515::disk;

namespace {

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

TEST_CASE("single-sided round-trip") {
    const auto files = diverseFiles();
    auto image = buildVolume(files);
    REQUIRE(image.size() == kSideSize);
    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    verifyFiles(*img, files);
}

TEST_CASE("double-sided: write + read back both sides byte-exact") {
    const auto side0 = diverseFiles();
    const std::vector<BuildFile> side1 = {
        {"MAGIC.DAT", pattern(900, 7)},            /* protection-side payload */
        {"BOOT2.SYS", std::vector<uint8_t>(2048, 0x5A)},
    };

    auto ds = buildDoubleSided(side0, side1);
    REQUIRE(ds.size() == kDoubleSize);

    auto s0 = openImage(ds, 0);
    auto s1 = openImage(ds, 1);
    REQUIRE(s0.has_value());
    REQUIRE(s1.has_value());
    verifyFiles(*s0, side0);
    verifyFiles(*s1, side1);
}

TEST_CASE("build leaves free sectors as the B6 6D blank pattern") {
    auto image = buildVolume(diverseFiles());   /* few small files near LBN 8 */
    const std::size_t off = lbnToByte(300, 0, false);   /* well past the files */
    CHECK(image[off + 0] == 0xB6);
    CHECK(image[off + 1] == 0x6D);
    CHECK(image[off + 2] == 0xB6);
    CHECK(image[off + 3] == 0x6D);
}

} /* TEST_SUITE */
