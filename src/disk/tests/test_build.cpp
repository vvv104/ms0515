/*
 * test_build.cpp — buildVolume round-trips through the parser.
 *
 * Self-consistency: files written by buildVolume must read back via
 * parseDirectory / Image::readFile with identical names, lengths and
 * (block-padded) bytes.  This catches writer bugs; the emulator-side
 * "does a real OS agree" check lives in the lib integration tests.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
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

void checkRoundTrip(Layout layout)
{
    const auto files = diverseFiles();
    auto image = buildVolume(layout, files);

    auto dir = parseDirectory(image, layout);
    REQUIRE(dir.has_value());
    auto perm = dir->permanentFiles();
    REQUIRE(perm.size() == files.size());

    Image img;
    img.data = image;
    img.layout = layout;
    img.directory = *dir;
    img.hasDirectory = true;

    for (const auto &f : files) {
        const DirEntry *e = dir->find(f.name);
        REQUIRE_MESSAGE(e != nullptr, "missing " << f.name);
        const int wantBlocks = static_cast<int>((f.data.size() + kBlock - 1) / kBlock);
        CHECK(e->length == wantBlocks);

        auto got = img.readFile(f.name);
        REQUIRE(got.size() == static_cast<std::size_t>(wantBlocks) * kBlock);
        /* original bytes present verbatim at the front */
        bool prefixOk = std::equal(f.data.begin(), f.data.end(), got.begin());
        CHECK_MESSAGE(prefixOk, "content mismatch for " << f.name);
        /* padding is zero */
        for (std::size_t i = f.data.size(); i < got.size(); ++i)
            CHECK(got[i] == 0);
    }
}

}  /* namespace */

TEST_SUITE("Build") {

TEST_CASE("round-trip ss-canonical") { checkRoundTrip(Layout::SsCanonical); }
TEST_CASE("round-trip ss-cyl0last-noil") { checkRoundTrip(Layout::SsCyl0LastNoIl); }
TEST_CASE("round-trip ss-osa-skew") { checkRoundTrip(Layout::SsOsaSkew); }
TEST_CASE("round-trip ds-cyl0last-noil") { checkRoundTrip(Layout::DsCyl0LastNoIl); }

TEST_CASE("double-sided dump: write + read back both sides byte-exact") {
    const Layout L = Layout::SsCyl0LastNoIl;   /* rodionov-style per-side */
    const auto side0 = diverseFiles();
    const std::vector<BuildFile> side1 = {
        {"MAGIC.DAT", pattern(900, 7)},            /* protection-side payload */
        {"BOOT2.SYS", std::vector<uint8_t>(2048, 0x5A)},
    };

    auto ds = buildDoubleSided(L, side0, side1);
    REQUIRE(ds.size() == static_cast<std::size_t>(kDoubleSize));

    auto verify = [&](int side, const std::vector<BuildFile> &files) {
        const auto begin = ds.begin() + static_cast<std::ptrdiff_t>(side) * kSideSize;
        auto img = openImage(std::vector<uint8_t>(begin, begin + kSideSize), side);
        REQUIRE_MESSAGE(img.has_value(), "side " << side << " did not open");
        REQUIRE_MESSAGE(img->hasDirectory, "side " << side << " has no directory");
        REQUIRE(img->directory.permanentFiles().size() == files.size());
        for (const auto &f : files) {
            auto got = img->readFile(f.name);
            const auto want = (f.data.size() + kBlock - 1) / kBlock * kBlock;
            REQUIRE(got.size() == want);
            CHECK_MESSAGE(std::equal(f.data.begin(), f.data.end(), got.begin()),
                          "side " << side << " file " << f.name << " mismatch");
        }
    };
    verify(0, side0);
    verify(1, side1);
}

TEST_CASE("layoutFromTag is the inverse of layoutTag") {
    using enum Layout;
    for (Layout l : {SsCanonical, SsOsaSkew, SsCyl0LastNoIl,
                     SsCyl0FirstNoIl, SsLbnLinear, DsCyl0LastNoIl})
        CHECK(layoutFromTag(layoutTag(l)) == l);
    CHECK_FALSE(layoutFromTag("nonsense").has_value());
}

} /* TEST_SUITE */
