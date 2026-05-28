/*
 * test_directory.cpp — RT-11 directory parsing.
 *
 * Uses synthetic in-memory segments (no disk fixtures) so the logic is
 * covered in CI where the recovery working data is absent.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Directory.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace ms0515::disk;

namespace {

void putw(std::vector<uint8_t> &b, size_t off, uint16_t v)
{
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>(v >> 8);
}

uint16_t rad50Word(const char *s)  /* exactly 3 chars from the RAD50 set */
{
    static const std::string set = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";
    auto idx = [&](char c) { return static_cast<uint16_t>(set.find(c)); };
    return static_cast<uint16_t>(idx(s[0]) * 1600 + idx(s[1]) * 40 + idx(s[2]));
}

/* Build a single 1024-byte segment with the given entries. Each entry is
 * (status, name3, name3, ext3, length). */
struct Ent { uint16_t status; const char *n1; const char *n2; const char *e;
             uint16_t length; };

std::vector<uint8_t> makeSegment(int dataStart, const std::vector<Ent> &ents)
{
    std::vector<uint8_t> seg(1024, 0);
    putw(seg, 0, 1);          /* segs_total */
    putw(seg, 2, 0);          /* next_seg = none */
    putw(seg, 4, 1);          /* highest_seg */
    putw(seg, 6, 0);          /* extra bytes */
    putw(seg, 8, static_cast<uint16_t>(dataStart));
    size_t p = 10;
    for (const auto &e : ents) {
        putw(seg, p + 0, e.status);
        putw(seg, p + 2, rad50Word(e.n1));
        putw(seg, p + 4, rad50Word(e.n2));
        putw(seg, p + 6, rad50Word(e.e));
        putw(seg, p + 8, e.length);
        putw(seg, p + 10, 0);   /* job/channel */
        putw(seg, p + 12, 0);   /* date */
        p += 14;
    }
    return seg;
}

}  /* namespace */

TEST_SUITE("Directory") {

TEST_CASE("RAD50 filename decode") {
    /* "FOO   " + "BAR" -> name; "TXT" ext */
    CHECK(decodeRad50Name(rad50Word("FOO"), rad50Word("   "),
                          rad50Word("TXT")) == "FOO.TXT");
    CHECK(decodeRad50Name(rad50Word("K  "), rad50Word("   "),
                          rad50Word("PAS")) == "K.PAS");
    /* no extension */
    CHECK(decodeRad50Name(rad50Word("ABC"), rad50Word("   "),
                          rad50Word("   ")) == "ABC");
}

TEST_CASE("parseSegment: two files + empty + end-of-segment") {
    auto seg = makeSegment(/*dataStart=*/8, {
        {kStatusPermanent, "AAA", "   ", "PAS", 3},
        {kStatusPermanent, "BBB", "   ", "OBJ", 5},
        {static_cast<uint16_t>(kStatusEmpty | kStatusEndOfSeg),
         "   ", "   ", "   ", 100},
    });
    auto d = parseSegment(seg);
    REQUIRE(d.has_value());
    CHECK(d->dataStart == 8);
    auto perm = d->permanentFiles();
    REQUIRE(perm.size() == 2);
    CHECK(perm[0].name == "AAA.PAS");
    CHECK(perm[0].startBlock == 8);
    CHECK(perm[0].length == 3);
    CHECK(perm[1].name == "BBB.OBJ");
    CHECK(perm[1].startBlock == 11);   /* 8 + 3 */
    CHECK(perm[1].length == 5);
    CHECK(d->find("bbb.obj") != nullptr);   /* case-insensitive */
    CHECK(d->find("ZZZ.ZZZ") == nullptr);
}

TEST_CASE("parseSegment rejects a non-directory block") {
    std::vector<uint8_t> junk(1024, 0xE5);
    CHECK_FALSE(parseSegment(junk).has_value());
    std::vector<uint8_t> zeros(1024, 0);
    CHECK_FALSE(parseSegment(zeros).has_value());
}

TEST_CASE("parseDirectory finds a segment through a layout") {
    /* Place a valid segment at the bytes ss-lbn-linear maps LBN 6 to,
     * i.e. byte 6*512=3072, and LBN 7 right after. */
    std::vector<uint8_t> img(kSideSize, 0);
    auto seg = makeSegment(8, {
        {kStatusPermanent, "ONE", "   ", "TXT", 2},
        {static_cast<uint16_t>(kStatusEmpty | kStatusEndOfSeg),
         "   ", "   ", "   ", 50},
    });
    std::copy(seg.begin(), seg.end(), img.begin() + 6 * 512);
    auto d = parseDirectory(img, Layout::SsLbnLinear);
    REQUIRE(d.has_value());
    CHECK(d->dirStartLbn == 6);
    REQUIRE(d->permanentFiles().size() == 1);
    CHECK(d->permanentFiles()[0].name == "ONE.TXT");
}

} /* TEST_SUITE */
