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

namespace {
/* The free blocks of a linear image: the empty entries' lengths summed. */
int im_free_blocks(const std::vector<uint8_t> &img) {
    const auto im = ms0515::disk::openLinearImage(img);   /* named: GCC 13 sees a temporary's member as maybe-uninitialized */
    int n = 0;
    if (im) for (const auto &e : im->directory.entries) if (e.isEmpty()) n += e.length;
    return n;
}
}

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

TEST_CASE("linear HD round-trip: blankLinear + init + put + read back") {
    auto img = blankLinear(4000);
    REQUIRE(img.size() == 4000u * kBlock);
    initVolume(img, 0, false, {}, Vol::linear);

    std::vector<uint8_t> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7 + 1);
    putFile(img, 0, false, "BIG.DAT", data, {}, Vol::linear);
    putFile(img, 0, false, "HI.TXT",
            std::vector<uint8_t>{'h', 'i'}, {}, Vol::linear);

    auto im = openLinearImage(img);
    REQUIRE(im.has_value());
    REQUIRE(im->hasDirectory);
    CHECK(im->vol == Vol::linear);
    REQUIRE(im->directory.permanentFiles().size() == 2);
    auto rd = im->readFile("BIG.DAT");
    REQUIRE(rd.size() >= data.size());
    CHECK(std::equal(data.begin(), data.end(), rd.begin()));

    /* The free area spans the whole 4000-block volume, not the 800-block
     * floppy side — proving the linear geometry generalises the size. */
    int freeLen = 0;
    for (const auto &e : im->directory.entries)
        if (e.isEmpty()) freeLen = e.length;
    CHECK(freeLen > 3000);
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

TEST_CASE("removeFile frees the slot and preserves the tail") {
    /* Build a volume with several files; remove one from the MIDDLE; verify
     * the others still read back byte-exact and the removed file is gone. */
    auto files = diverseFiles();
    auto image = makeVolume(false, 0, files);

    const std::string victim = "RND.BIN";       /* 1500 B / 3 blocks, in the middle */
    removeFile(image, 0, false, victim);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    CHECK(img->directory.find(victim) == nullptr);

    std::vector<BuildFile> remaining;
    for (const auto &f : files) if (f.name != victim) remaining.push_back(f);
    verifyFiles(*img, remaining);
}

TEST_CASE("removeFile + putFile of the same size lands in the freed slot") {
    /* The freed slot must be reusable — putFile finds the empty entry the
     * remove left behind, drops a same-size replacement in, and the tail
     * survives unchanged (the regression that ate 5 files in disk1.dsk). */
    auto image = makeVolume(false, 0, diverseFiles());
    auto before = openImage(image, 0);
    REQUIRE(before.has_value());
    const DirEntry *victimEntry = before->directory.find("RND.BIN");
    REQUIRE(victimEntry != nullptr);
    const int victimStart  = victimEntry->startBlock;
    const int victimLength = victimEntry->length;

    removeFile(image, 0, false, "RND.BIN");

    /* Same length (3 blocks), distinct bytes so we can prove which copy is read. */
    const auto replacement = pattern(static_cast<std::size_t>(victimLength) * kBlock, 99);
    putFile(image, 0, false, "RND.BIN", replacement);

    auto after = openImage(image, 0);
    REQUIRE(after.has_value());
    /* Every original file still listed, no duplicates, no loss. */
    auto expected = diverseFiles();
    for (auto &f : expected) if (f.name == "RND.BIN") f.data = replacement;
    verifyFiles(*after, expected);
    /* And the replacement sits in the SAME data blocks the original used. */
    const DirEntry *re = after->directory.find("RND.BIN");
    REQUIRE(re != nullptr);
    CHECK(re->startBlock == victimStart);
    CHECK(re->length     == victimLength);
}

TEST_CASE("removeFile + putFile of a smaller size leaves a residual empty") {
    /* Smaller replacement: the freed slot splits into (new file)+(residual empty)
     * and the tail entries still read back without loss. */
    auto image = makeVolume(false, 0, diverseFiles());
    removeFile(image, 0, false, "RND.BIN");           /* 3-block slot freed */

    const auto smaller = pattern(kBlock, 5);          /* 1 block */
    putFile(image, 0, false, "SMALL.X", smaller);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    auto expected = diverseFiles();
    expected.erase(std::remove_if(expected.begin(), expected.end(),
                                  [](const BuildFile &f) { return f.name == "RND.BIN"; }),
                   expected.end());
    expected.push_back({"SMALL.X", smaller});
    verifyFiles(*img, expected);
}

TEST_CASE("putFile throws when no empty slot fits, even after a rm") {
    /* Fill the volume so the tail empty is too small, then rm a tiny file
     * to leave a hole that's also too small.  putFile of a bigger file
     * must throw — neither the freed hole nor the tail empty has room. */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    /* Single-sided RT-11 volume has ~785 free blocks after init. Take most
     * of it with two big files, leaving a small tail and a small hole. */
    putFile(image, 0, false, "BIG1.X", pattern(380 * kBlock, 1));   /* 380 blocks */
    putFile(image, 0, false, "HOLE.X", pattern(  2 * kBlock, 2));   /* 2 blocks  */
    putFile(image, 0, false, "BIG2.X", pattern(400 * kBlock, 3));   /* 400 blocks → tail ~2 blocks */
    removeFile(image, 0, false, "HOLE.X");                           /* hole: 2 blocks */

    /* 10 blocks fits NEITHER the 2-block hole NOR the ~2-block tail. */
    const auto too_big = pattern(10 * kBlock, 9);
    CHECK_THROWS_AS(putFile(image, 0, false, "TOOBIG.X", too_big), std::runtime_error);
}

TEST_CASE("putFile skips an undersized middle empty for a fitting later slot") {
    /* After removeFile leaves a small hole in the middle, a subsequent put
     * of a larger file must scan PAST the hole and land in the tail's free
     * area instead of failing with "does not fit".  Earlier put() bailed at
     * the first empty it saw. */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    putFile(image, 0, false, "ALPHA.X",   pattern(2 * kBlock, 1));    /* 2 blocks */
    putFile(image, 0, false, "VICTIM.X",  pattern(2 * kBlock, 2));    /* 2 blocks — to delete */
    putFile(image, 0, false, "BRAVO.X",   pattern(3 * kBlock, 3));    /* 3 blocks */
    removeFile(image, 0, false, "VICTIM.X");                          /* 2-block hole in middle */

    /* A 4-block file does NOT fit the 2-block hole.  Must land in the tail
     * empty, NOT throw.  Existing files survive. */
    const auto big = pattern(4 * kBlock, 4);
    putFile(image, 0, false, "BIG.X", big);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    verifyFiles(*img, {
        {"ALPHA.X", pattern(2 * kBlock, 1)},
        {"BRAVO.X", pattern(3 * kBlock, 3)},
        {"BIG.X",   big},
    });
}

TEST_CASE("putFile prefers the middle hole when it fits") {
    /* Inverse of the above: a small enough new file should land in the freed
     * hole, not at the end (so the tool matches PIP's behaviour and keeps
     * the volume from fragmenting). */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    putFile(image, 0, false, "ALPHA.X",  pattern(2 * kBlock, 1));
    putFile(image, 0, false, "VICTIM.X", pattern(3 * kBlock, 2));
    putFile(image, 0, false, "BRAVO.X",  pattern(2 * kBlock, 3));
    auto before = openImage(image, 0);
    const int victimStart = before->directory.find("VICTIM.X")->startBlock;
    removeFile(image, 0, false, "VICTIM.X");                          /* 3-block hole */

    const auto fits = pattern(2 * kBlock, 4);                         /* 2 < 3 */
    putFile(image, 0, false, "NEW.X", fits);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    const DirEntry *ne = img->directory.find("NEW.X");
    REQUIRE(ne != nullptr);
    CHECK_MESSAGE(ne->startBlock == victimStart,
                  "new file should reuse the freed slot");
}

TEST_CASE("removeFile of the only file leaves the volume empty + reusable") {
    /* Single-file case: after rm, the directory has TWO adjacent empty entries
     * (the freed slot + the original residual free area).  Both must parse
     * back, the volume must list zero permanent files, and a subsequent put
     * must work — sized to either slot.  The regression here would be a put
     * that misreads the chain of empties and corrupts the directory. */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    const auto orig = pattern(5 * kBlock, 33);              /* 5-block file */
    putFile(image, 0, false, "SOLO.X", orig);

    removeFile(image, 0, false, "SOLO.X");

    auto post_rm = openImage(image, 0);
    REQUIRE(post_rm.has_value());
    CHECK(post_rm->directory.permanentFiles().empty());

    /* Put a SAME-SIZE replacement: must land in the first empty (SOLO's
     * freed slot), volume should read just that one file. */
    const auto same = pattern(5 * kBlock, 44);
    putFile(image, 0, false, "BACK.X", same);
    auto img1 = openImage(image, 0);
    REQUIRE(img1.has_value());
    verifyFiles(*img1, {{"BACK.X", same}});

    /* Reset and put a SMALLER replacement so the freed slot splits into
     * (file)+(residual empty) and the original tail empty stays untouched. */
    auto image2 = blankImage(false);
    initVolume(image2, 0, false);
    putFile(image2, 0, false, "SOLO.X", orig);
    removeFile(image2, 0, false, "SOLO.X");
    const auto small = pattern(2 * kBlock, 55);
    putFile(image2, 0, false, "TINY.X", small);
    auto img2 = openImage(image2, 0);
    REQUIRE(img2.has_value());
    verifyFiles(*img2, {{"TINY.X", small}});
}

TEST_CASE("decodeDate is the inverse of encodeDate") {
    /* Round-trip every valid (year, month, day) over a sweep of representative
     * boundary points: epoch start, age-overflow boundary, last day, and a
     * date in the middle of the range we actually use (1994-02-18). */
    struct YMD { int y, m, d; };
    const YMD cases[] = {
        {1972, 1,  1},   /* epoch start */
        {1994, 2, 18},   /* the VVV LINK.SAV date */
        {1995, 4,  1},   /* the VVV system-wide date */
        {2003,12, 31},   /* last value with age=0 */
        {2004, 1,  1},   /* age rolls to 1 */
        {2099,12, 31},   /* last representable date */
    };
    for (const auto &c : cases) {
        const uint16_t w  = encodeDate(c.y, c.m, c.d);
        const auto     dp = decodeDate(w);
        CHECK_MESSAGE(dp.year  == c.y, "year mismatch on "  << c.y << "-" << c.m << "-" << c.d);
        CHECK_MESSAGE(dp.month == c.m, "month mismatch on " << c.y << "-" << c.m << "-" << c.d);
        CHECK_MESSAGE(dp.day   == c.d, "day mismatch on "   << c.y << "-" << c.m << "-" << c.d);
    }
    /* Zero is the "no date" sentinel both directions. */
    const auto zero = decodeDate(0);
    CHECK(zero.year == 0);
    CHECK(zero.month == 0);
    CHECK(zero.day == 0);
}

TEST_CASE("encodeDate packs RT-11 directory date words") {
    /* Bit layout (high→low): 2-bit age | 4-bit month | 5-bit day | 5-bit year. */
    CHECK(encodeDate(0, 0, 0) == 0);               /* "no date" sentinel */

    /* 1994-02-18 — the original date on VVV disk1's LINK.SAV: 0x0A56. */
    const uint16_t w = encodeDate(1994, 2, 18);
    CHECK(w == 0x0A56);
    CHECK(((w >> 14) & 0x03) == 0);                /* age */
    CHECK(((w >> 10) & 0x0F) == 2);                /* month */
    CHECK(((w >>  5) & 0x1F) == 18);               /* day */
    CHECK(( w        & 0x1F) == 22);               /* year - 1972 */

    /* 2004-01-01 — wraps into age=1 (year_bits = 0). */
    const uint16_t w2 = encodeDate(2004, 1, 1);
    CHECK(((w2 >> 14) & 0x03) == 1);
    CHECK(( w2        & 0x1F) == 0);

    /* 2099-12-31 — the last representable date. */
    CHECK_NOTHROW(static_cast<void>(encodeDate(2099, 12, 31)));

    CHECK_THROWS_AS(static_cast<void>(encodeDate(1971, 1,  1)), std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(encodeDate(2100, 1,  1)), std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(encodeDate(1994, 0,  1)), std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(encodeDate(1994, 13, 1)), std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(encodeDate(1994, 1, 32)), std::runtime_error);
}

TEST_CASE("putFile applies the date and protected flag from PutOptions") {
    auto image = blankImage(false);
    initVolume(image, 0, false);
    const auto data = pattern(2 * kBlock, 1);
    PutOptions opts;
    opts.date     = encodeDate(1994, 2, 18);
    opts.readOnly = true;
    putFile(image, 0, false, "SYS.SAV", data, opts);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    const DirEntry *e = img->directory.find("SYS.SAV");
    REQUIRE(e != nullptr);
    CHECK((e->status & kStatusPermanent) != 0);
    CHECK((e->status & kStatusProtected) != 0);
    CHECK(e->date == 0x0A56);
}

TEST_CASE("setProtected toggles the flag without touching anything else") {
    auto image = blankImage(false);
    initVolume(image, 0, false);
    const auto data = pattern(kBlock, 7);
    putFile(image, 0, false, "F.X", data);

    setProtected(image, 0, false, "F.X", true);
    {
        auto img = openImage(image, 0);
        const DirEntry *e = img->directory.find("F.X");
        REQUIRE(e != nullptr);
        CHECK((e->status & kStatusProtected) != 0);
        CHECK((e->status & kStatusPermanent) != 0);
        CHECK(img->readFile("F.X").size() >= data.size());
    }

    setProtected(image, 0, false, "F.X", false);
    {
        auto img = openImage(image, 0);
        const DirEntry *e = img->directory.find("F.X");
        REQUIRE(e != nullptr);
        CHECK((e->status & kStatusProtected) == 0);
        CHECK((e->status & kStatusPermanent) != 0);
    }

    CHECK_THROWS_AS(setProtected(image, 0, false, "NOPE.X", true), std::runtime_error);
}

TEST_CASE("setEntryDate writes the date in place") {
    auto image = blankImage(false);
    initVolume(image, 0, false);
    putFile(image, 0, false, "F.X", pattern(kBlock, 7));
    setEntryDate(image, 0, false, "F.X", encodeDate(1994, 2, 18));

    auto img = openImage(image, 0);
    const DirEntry *e = img->directory.find("F.X");
    REQUIRE(e != nullptr);
    CHECK(e->date == 0x0A56);

    /* Clear back to "no date". */
    setEntryDate(image, 0, false, "F.X", 0);
    auto img2 = openImage(image, 0);
    CHECK(img2->directory.find("F.X")->date == 0);
}

TEST_CASE("squeeze packs files contiguously and preserves metadata") {
    /* Put several files, delete a couple from the middle, squeeze, and check:
     *   - permanents stay in directory order with their bytes intact;
     *   - data blocks are contiguous starting at data_start;
     *   - the directory ends with ONE empty entry covering all freed space;
     *   - status flags + dates survive the move. */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    PutOptions opts1; opts1.date = encodeDate(1994, 2, 18);
    PutOptions opts2; opts2.date = encodeDate(1995, 6, 30); opts2.readOnly = true;
    PutOptions opts3; opts3.date = encodeDate(2024, 12, 31);
    PutOptions opts4; opts4.date = encodeDate(1996, 7, 14);
    const auto A = pattern(3 * kBlock, 0x11);
    const auto B = pattern(2 * kBlock, 0x22);
    const auto C = pattern(5 * kBlock, 0x33);
    const auto D = pattern(1 * kBlock, 0x44);
    putFile(image, 0, false, "A.X", A, opts1);
    putFile(image, 0, false, "B.X", B, opts2);
    putFile(image, 0, false, "C.X", C, opts3);
    putFile(image, 0, false, "D.X", D, opts4);
    removeFile(image, 0, false, "B.X");
    removeFile(image, 0, false, "C.X");                /* two holes between A and D */

    squeeze(image, 0, false);

    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    const auto perms = img->directory.permanentFiles();
    REQUIRE(perms.size() == 2);
    CHECK(perms[0].name == "A.X");
    CHECK(perms[1].name == "D.X");
    /* Contiguous: D starts where A ended. */
    CHECK(perms[1].startBlock == perms[0].startBlock + perms[0].length);
    /* Metadata survived. */
    CHECK(perms[0].date == 0x0A56);
    CHECK(perms[1].date == encodeDate(1996, 7, 14));
    /* Bytes survived. */
    auto a = img->readFile("A.X");
    auto d = img->readFile("D.X");
    REQUIRE(a.size() >= A.size());
    REQUIRE(d.size() >= D.size());
    CHECK(std::equal(A.begin(), A.end(), a.begin()));
    CHECK(std::equal(D.begin(), D.end(), d.begin()));
}

TEST_CASE("squeeze on a volume with no holes is a no-op") {
    auto image = blankImage(false);
    initVolume(image, 0, false);
    const auto data = pattern(4 * kBlock, 9);
    putFile(image, 0, false, "ONE.X",   data);
    putFile(image, 0, false, "TWO.X",   pattern(2 * kBlock, 8));
    putFile(image, 0, false, "THREE.X", pattern(3 * kBlock, 7));
    auto before = image;
    squeeze(image, 0, false);
    /* The data blocks shouldn't move; the directory should be equivalent (PIP
     * actually rewrites the segment header on no-op squeeze too, so we don't
     * compare images byte-for-byte — just that the files still read back). */
    auto img = openImage(image, 0);
    REQUIRE(img.has_value());
    CHECK(img->directory.permanentFiles().size() == 3);
    CHECK(std::equal(data.begin(), data.end(), img->readFile("ONE.X").begin()));
}

TEST_CASE("squeeze + putFile uses the consolidated free area") {
    /* After squeeze, the trailing empty should hold all the previously
     * scattered free space — a put that wouldn't fit in any single old hole
     * should now succeed. */
    auto image = blankImage(false);
    initVolume(image, 0, false);
    putFile(image, 0, false, "A.X", pattern(2 * kBlock, 1));
    putFile(image, 0, false, "B.X", pattern(2 * kBlock, 2));
    putFile(image, 0, false, "C.X", pattern(2 * kBlock, 3));
    removeFile(image, 0, false, "B.X");                /* 2-block hole */
    squeeze(image, 0, false);

    /* A and C are now contiguous; the rest of the side is one big empty. */
    const auto big = pattern(700 * kBlock, 5);
    CHECK_NOTHROW(putFile(image, 0, false, "BIG.X", big));
}

TEST_CASE("squeeze errors") {
    SUBCASE("uninitialised volume") {
        auto raw = blankImage(false);
        CHECK_THROWS_AS(squeeze(raw, 0, false), std::runtime_error);
    }
    SUBCASE("wrong image size for the ss/ds flag") {
        auto ss = blankImage(false);
        initVolume(ss, 0, false);
        CHECK_THROWS_AS(squeeze(ss, 0, true), std::runtime_error);
    }
}

TEST_CASE("removeFile errors") {
    auto image = makeVolume(false, 0, diverseFiles());

    SUBCASE("file not found") {
        CHECK_THROWS_AS(removeFile(image, 0, false, "NOPE.DAT"), std::runtime_error);
    }
    SUBCASE("not initialised") {
        auto raw = blankImage(false);
        CHECK_THROWS_AS(removeFile(raw, 0, false, "X.DAT"), std::runtime_error);
    }
    SUBCASE("illegal (non-RAD50) character in name") {
        CHECK_THROWS_AS(removeFile(image, 0, false, "BAD-NM.X"), std::runtime_error);
    }
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

TEST_CASE("put / rm on a volume whose home block does not point at the directory") {
    /* The machine's own INIT (OSA, Omega, ...) leaves other things in the
     * home block's directory word; dir/get find segment 1 by scanning, and
     * put/rm must follow them rather than refuse the volume. */
    auto img = blankImage(false);
    initVolume(img, 0, false);
    putFile(img, 0, false, "FIRST.TXT", pattern(700, 1));
    img[512 + 0x1D4] = 0; img[512 + 0x1D5] = 0;           /* LBN 1, word 0x1D4 */
    putFile(img, 0, false, "SECOND.TXT", pattern(1500, 2));
    auto opened = openImage(img, 0);
    REQUIRE(opened);
    CHECK(opened->directory.find("FIRST.TXT") != nullptr);
    CHECK(opened->directory.find("SECOND.TXT") != nullptr);
    CHECK(opened->readFile("SECOND.TXT").size() == 1536);
    removeFile(img, 0, false, "FIRST.TXT");
    opened = openImage(img, 0);
    REQUIRE(opened);
    CHECK(opened->directory.find("FIRST.TXT") == nullptr);
    CHECK(opened->directory.find("SECOND.TXT") != nullptr);
    img[512 + 0x1D4] = 0xFF; img[512 + 0x1D5] = 0x7F;      /* out of the volume */
    setEntryDate(img, 0, false, "SECOND.TXT", encodeDate(1992, 8, 22));
    opened = openImage(img, 0);
    REQUIRE(opened);
    CHECK(opened->directory.find("SECOND.TXT")->date == encodeDate(1992, 8, 22));
}

TEST_CASE("rename keeps the data and refuses a taken or bad name") {
    auto img = blankImage(false);
    initVolume(img, 0, false);
    putFile(img, 0, false, "OLD.TXT", pattern(700, 3), PutOptions{encodeDate(1992, 8, 22), false});
    putFile(img, 0, false, "OTHER.DAT", pattern(300, 4));
    renameFile(img, 0, false, "OLD.TXT", "NEW.TXT");
    auto opened = openImage(img, 0);
    REQUIRE(opened);
    CHECK(opened->directory.find("OLD.TXT") == nullptr);
    const DirEntry *e = opened->directory.find("NEW.TXT");
    REQUIRE(e != nullptr);
    CHECK(e->date == encodeDate(1992, 8, 22));
    CHECK(e->length == 2);
    auto data = opened->readFile("NEW.TXT");
    CHECK(std::equal(data.begin(), data.begin() + 700, pattern(700, 3).begin()));
    CHECK_THROWS(renameFile(img, 0, false, "NEW.TXT", "OTHER.DAT"));
    CHECK_THROWS(renameFile(img, 0, false, "NEW.TXT", "TOOLONGNAME.TXT"));
    CHECK_THROWS(renameFile(img, 0, false, "NOSUCH.TXT", "X.TXT"));
}

TEST_CASE("removeFile merges the empty entries around the one freed, as the OS does; a file put back larger stays put") {
    auto img = blankLinear(40);
    InitOptions one;
    one.segments = 1;
    initVolume(img, 0, false, one, Vol::linear);            /* 32 data blocks from 8 */
    const std::vector<uint8_t> a(kBlock, 1), b(3 * kBlock, 2), c(2 * kBlock, 3), d(kBlock, 4);
    PutOptions dated;
    dated.date = encodeDate(1989, 1, 5);
    putFile(img, 0, false, "A.DAT", a, dated, Vol::linear);
    putFile(img, 0, false, "B.DAT", b, {}, Vol::linear);
    putFile(img, 0, false, "C.DAT", c, {}, Vol::linear);
    putFile(img, 0, false, "D.DAT", d, {}, Vol::linear);   /* then 25 free */
    auto entries = [&] { const auto im = openLinearImage(img); return im ? im->directory.entries : std::vector<DirEntry>{}; };   /* the end marker counted */
    REQUIRE(entries().size() == 6);

    /* B between two files: its entry stays; A before it: A absorbs it. */
    removeFile(img, 0, false, "B.DAT", Vol::linear);
    CHECK(entries().size() == 6);
    removeFile(img, 0, false, "A.DAT", Vol::linear);
    auto e = entries();
    REQUIRE(e.size() == 5);
    CHECK(e[0].isEmpty()); CHECK(e[0].name == "A.DAT"); CHECK(e[0].length == 4); CHECK(e[0].date == dated.date);
    CHECK(e[1].name == "C.DAT"); CHECK(e[1].isPermanent());
    CHECK(im_free_blocks(img) == 29);

    /* D, the last file, is followed by the free tail: one area of 26. */
    removeFile(img, 0, false, "D.DAT", Vol::linear);
    e = entries();
    REQUIRE(e.size() == 4);
    CHECK(e[2].isEmpty()); CHECK(e[2].name == "D.DAT"); CHECK(e[2].length == 26);

    /* C, empty on both sides now: everything one area, the earliest name. */
    removeFile(img, 0, false, "C.DAT", Vol::linear);
    e = entries();
    REQUIRE(e.size() == 2);
    CHECK(e[0].isEmpty()); CHECK(e[0].name == "A.DAT"); CHECK(e[0].length == 32);
    CHECK(openLinearImage(img)->readFile("A.DAT").empty());

    /* A file put back larger, with room behind it, lands where it was. */
    putFile(img, 0, false, "X.DAT", c, {}, Vol::linear);
    putFile(img, 0, false, "Y.DAT", d, {}, Vol::linear);
    const int xAt = openLinearImage(img)->directory.find("X.DAT")->startBlock;
    removeFile(img, 0, false, "Y.DAT", Vol::linear);          /* Y and the tail: one area behind X */
    removeFile(img, 0, false, "X.DAT", Vol::linear);          /* X joins it: one area from X's start */
    putFile(img, 0, false, "X.DAT", std::vector<uint8_t>(5 * kBlock, 9), {}, Vol::linear);
    CHECK(openLinearImage(img)->directory.find("X.DAT")->startBlock == xAt);
    CHECK(openLinearImage(img)->directory.find("X.DAT")->length == 5);
}

TEST_CASE("setVolumeId writes the home block's id and owner and leaves the files") {
    auto img = blankLinear(40);
    CHECK_THROWS(setVolumeId(img, 0, false, "X", "Y", Vol::linear));     /* not initialised */
    InitOptions o;
    o.volumeId = "RT11A";
    o.segments = 2;
    initVolume(img, 0, false, o, Vol::linear);
    const std::vector<uint8_t> a(kBlock, 7);
    putFile(img, 0, false, "A.DAT", a, {}, Vol::linear);
    auto field = [&](int off) { std::string s(reinterpret_cast<const char *>(img.data()) + kBlock + off, 12); return s; };
    CHECK(field(0x1D8) == "RT11A       ");
    setVolumeId(img, 0, false, "PROGRAMS", "VVV", Vol::linear);
    CHECK(field(0x1D8) == "PROGRAMS    ");
    CHECK(field(0x1E4) == "VVV         ");
    auto im = openLinearImage(img);
    REQUIRE(im->hasDirectory);
    CHECK(im->directory.segsTotal == 2);
    CHECK(im->readFile("A.DAT") == a);
    CHECK_THROWS(setVolumeId(img, 0, false, "THIRTEEN-CHAR", "", Vol::linear));
    setVolumeId(img, 0, false, "", "", Vol::linear);
    CHECK(field(0x1D8) == "            ");
}

namespace {
/* What a call throws, "" when nothing. */
template <class F> std::string thrown(F f)
{
    try { f(); } catch (const std::exception &e) { return e.what(); }
    return "";
}

/* A make-believe kit on a fresh floppy: DZ.SYS with a primary driver at
 * block 1 (word 062 = 01000, 066 = 0140) and a 6-block monitor whose block
 * 4 is zero where DUP writes. */
std::vector<uint8_t> kitFloppy()
{
    auto img = blankImage(false);
    initVolume(img, 0, false);
    std::vector<uint8_t> dz(3 * kBlock, 0);
    dz[062] = 0x00; dz[063] = 0x02;                       /* 01000: the driver starts at block 1 */
    dz[064] = 0x00; dz[065] = 0x02;                       /* its length: one block */
    dz[066] = 0x60; dz[067] = 0x00;                       /* the read routine at 0140 */
    for (std::size_t i = 0; i < kBlock; ++i) dz[kBlock + i] = static_cast<uint8_t>(0xA0 + i % 64);
    putFile(img, 0, false, "DZ.SYS", dz);
    std::vector<uint8_t> mon(6 * kBlock);
    for (std::size_t i = 0; i < mon.size(); ++i) mon[i] = static_cast<uint8_t>(i / kBlock * 16 + i % 16 + 1);
    for (std::size_t i = 4 * kBlock; i < 5 * kBlock; ++i) mon[i] = 0;
    putFile(img, 0, false, "RT11SJ.SYS", mon);
    putFile(img, 0, false, "SWAP.SYS", std::vector<uint8_t>(kBlock, 3));
    putFile(img, 0, false, "PIP.SAV", std::vector<uint8_t>(kBlock, 4));
    putFile(img, 0, false, "GAME.SAV", std::vector<uint8_t>(kBlock, 5));
    return img;
}
}  /* namespace */

TEST_CASE("writeBoot: the handler's primary driver to LBN 0, the monitor's blocks 1..4 to LBN 2..5, four words named") {
    auto img = kitFloppy();
    CHECK(bootedMonitor(img, 0, false) == "");
    CHECK(systemKit(img, 0, false, "RT11SJ") == std::vector<std::string>{"DZ.SYS", "RT11SJ.SYS", "SWAP.SYS", "PIP.SAV"});
    CHECK(systemKit(img, 0, false, "MON8SJ") == std::vector<std::string>{"DZ.SYS", "SWAP.SYS", "PIP.SAV"});
    const auto before = img;

    writeBoot(img, 0, false, "RT11SJ");
    auto lbn = [&](int n) { return std::vector<uint8_t>(img.begin() + static_cast<std::ptrdiff_t>(lbnToByte(n, 0, false)),
                                                        img.begin() + static_cast<std::ptrdiff_t>(lbnToByte(n, 0, false)) + static_cast<std::ptrdiff_t>(kBlock)); };
    auto im = openImage(img, 0);
    const auto dz = im->readFile("DZ.SYS"), mon = im->readFile("RT11SJ.SYS");
    CHECK(std::equal(dz.begin() + kBlock, dz.begin() + 2 * kBlock, lbn(0).begin()));
    CHECK(std::equal(mon.begin() + kBlock, mon.begin() + 2 * kBlock, lbn(2).begin()));
    CHECK(std::equal(mon.begin() + 2 * kBlock, mon.begin() + 3 * kBlock, lbn(3).begin()));
    CHECK(std::equal(mon.begin() + 3 * kBlock, mon.begin() + 4 * kBlock, lbn(4).begin()));
    const auto b5 = lbn(5);
    auto w = [&](std::size_t off) { return b5[off] | (b5[off + 1] << 8); };
    CHECK(w(0716) == 016420);            /* RAD50 "DZ " */
    CHECK(w(0724) == 071677);            /* "RT1" */
    CHECK(w(0726) == 0142302);           /* "1SJ" */
    CHECK(w(0730) == 0140);              /* the handler's word 066 */
    int others = 0;
    for (std::size_t i = 0; i < kBlock; i += 2) if (i != 0716 && i != 0724 && i != 0726 && i != 0730 && w(i) != 0) ++others;
    CHECK(others == 0);
    /* Only those five blocks changed: the home block and the files as they were. */
    for (int n = 1; n < 14; ++n) if (n != 2 && n != 3 && n != 4 && n != 5) {
        const auto at = lbnToByte(n, 0, false);
        CHECK(std::equal(img.begin() + static_cast<std::ptrdiff_t>(at), img.begin() + static_cast<std::ptrdiff_t>(at + kBlock), before.begin() + static_cast<std::ptrdiff_t>(at)));
    }
    CHECK(bootedMonitor(img, 0, false) == "RT11SJ");
    CHECK(openImage(img, 0)->readFile("GAME.SAV") == std::vector<uint8_t>(kBlock, 5));

    /* ".SYS" given or not, the same; doing it again changes nothing. */
    const auto once = img;
    writeBoot(img, 0, false, "RT11SJ.SYS");
    CHECK(img == once);
}

TEST_CASE("startupFile: the KMON line the monitor holds") {
    std::vector<uint8_t> m(3000, 0x55);
    CHECK(startupFile(m) == "");
    const char a[] = "\0@STARTS\0";
    std::copy(a, a + 9, m.begin() + 1000);
    CHECK(startupFile(m) == "STARTS.COM");
    const char b[] = "\0@ST    \0";
    std::copy(b, b + 9, m.begin() + 1000);
    CHECK(startupFile(m) == "ST.COM");
    const char c[] = "\0@START \0";
    std::copy(c, c + 9, m.begin() + 1000);
    CHECK(startupFile(m) == "START.COM");
    const char d[] = "\0@$     \0";                        /* one character: not a file */
    std::copy(d, d + 9, m.begin() + 1000);
    CHECK(startupFile(m) == "");
}

TEST_CASE("makeSystemVolume: the kit protected, the extras as they are, the startup file made anew, the bootstrap") {
    auto system = kitFloppy();
    {   /* the monitor names ST as its startup file; a VM.SYS and a game lie about */
        auto mon = openImage(system, 0)->readFile("RT11SJ.SYS");
        const char line[] = "\0@ST    \0";
        std::copy(line, line + 9, mon.begin() + 5 * kBlock + 100);
        removeFile(system, 0, false, "RT11SJ.SYS");
        putFile(system, 0, false, "RT11SJ.SYS", mon);
        putFile(system, 0, false, "VM.SYS", std::vector<uint8_t>(kBlock, 6));
        putFile(system, 0, false, "TT.SYS", std::vector<uint8_t>(kBlock, 7));
        putFile(system, 0, false, "ST.COM", std::vector<uint8_t>{'R', ' ', 'G', 'A', 'M', 'E', '\r', '\n'});
        writeBoot(system, 0, false, "RT11SJ");
    }
    auto target = blankImage(false);
    initVolume(target, 0, false);
    CHECK(makeSystemVolume(target, 0, false, system, 0, false, {"GAME.SAV"}) == "RT11SJ");
    auto im = openImage(target, 0);
    std::vector<std::string> names;
    for (const auto &e : im->directory.permanentFiles()) names.push_back(e.name);
    CHECK(names == std::vector<std::string>{"DZ.SYS", "RT11SJ.SYS", "SWAP.SYS", "PIP.SAV", "TT.SYS", "GAME.SAV", "ST.COM"});
    for (const auto &e : im->directory.permanentFiles()) {
        const bool protectedNow = (e.status & kStatusProtected) != 0;
        CHECK_MESSAGE(protectedNow == (e.name != "GAME.SAV" && e.name != "ST.COM"), e.name);
    }
    const auto startup = im->readFile("ST.COM");
    CHECK(startup.size() == kBlock);
    CHECK(std::string(startup.begin(), startup.begin() + 14) == "SET TT QUIET\r\n");
    CHECK(bootedMonitor(target, 0, false) == "RT11SJ");
    CHECK(openImage(target, 0)->readFile("GAME.SAV") == std::vector<uint8_t>(kBlock, 5));

    /* Done again: the files replaced, the startup file there kept. */
    auto st = openImage(target, 0)->readFile("ST.COM"); st[0] = '!';
    removeFile(target, 0, false, "ST.COM"); putFile(target, 0, false, "ST.COM", st);
    makeSystemVolume(target, 0, false, system, 0, false);
    CHECK(openImage(target, 0)->readFile("ST.COM")[0] == '!');
    CHECK(openImage(target, 0)->directory.permanentFiles().size() == 7);

    auto blank = blankImage(false);
    CHECK_THROWS(makeSystemVolume(blank, 0, false, system, 0, false));          /* no directory on the target */
    auto unbootable = kitFloppy();
    initVolume(target, 0, false);
    CHECK_THROWS(makeSystemVolume(target, 0, false, unbootable, 0, false));     /* the source does not boot */
}

TEST_CASE("writeBoot refuses what it cannot make bootable") {
    auto img = kitFloppy();
    CHECK(thrown([&] { writeBoot(img, 0, false, "MON8SJ"); }).find("no MON8SJ.SYS") != std::string::npos);
    auto noDriver = img;
    {   /* a DZ.SYS with no .DRBOT words: a data-device handler */
        std::vector<uint8_t> dz(3 * kBlock, 0);
        removeFile(noDriver, 0, false, "DZ.SYS");
        putFile(noDriver, 0, false, "DZ.SYS", dz);
        CHECK(thrown([&] { writeBoot(noDriver, 0, false, "RT11SJ"); }).find("no primary driver") != std::string::npos);
    }
    auto busy = img;
    {   /* a monitor whose block 4 has something where DUP writes */
        auto mon = openImage(busy, 0)->readFile("RT11SJ.SYS");
        mon[4 * kBlock + 0724] = 1;
        removeFile(busy, 0, false, "RT11SJ.SYS");
        putFile(busy, 0, false, "RT11SJ.SYS", mon);
        CHECK(thrown([&] { writeBoot(busy, 0, false, "RT11SJ"); }).find("another layout") != std::string::npos);
    }
    auto blank = blankImage(false);
    CHECK_THROWS(writeBoot(blank, 0, false, "RT11SJ"));
    auto linear = blankLinear(100);
    CHECK_THROWS(writeBoot(linear, 0, false, "RT11SJ"));
    CHECK(bootedMonitor(blank, 0, false) == "");
}

TEST_CASE("growLinear: a full volume gets a new empty entry, a free one a longer last area") {
    auto img = blankLinear(20);
    InitOptions one;
    one.segments = 1;
    initVolume(img, 0, false, one, Vol::linear);      /* data from block 8: 12 free */
    const std::vector<uint8_t> big(12 * kBlock, 0x5A);
    putFile(img, 0, false, "FULL.DAT", big, {}, Vol::linear);
    CHECK_THROWS(putFile(img, 0, false, "MORE.DAT", std::vector<uint8_t>(kBlock, 1), {}, Vol::linear));

    growLinear(img, 10);
    CHECK(img.size() == 30u * kBlock);
    putFile(img, 0, false, "MORE.DAT", std::vector<uint8_t>(10 * kBlock, 1), {}, Vol::linear);
    auto im = openLinearImage(img);
    REQUIRE(im.has_value());
    REQUIRE(im->hasDirectory);
    CHECK(im->directory.permanentFiles().size() == 2);
    CHECK(im->readFile("FULL.DAT") == big);

    auto freeArea = [&] {
        im = openLinearImage(img);
        int blocks = 0, empties = 0;
        for (const auto &e : im->directory.entries)
            if (e.isEmpty()) { blocks += e.length; ++empties; }
        return std::pair{blocks, empties};
    };
    growLinear(img, 5);
    CHECK(freeArea() == std::pair{5, 1});
    growLinear(img, 3);                                    /* the same empty entry, longer */
    CHECK(freeArea() == std::pair{8, 1});
    CHECK(img.size() == 38u * kBlock);
    CHECK_THROWS(growLinear(img, 0));
}

TEST_CASE("removeFile keeps the name and the date in the entry as the OS does; undeleteEntry brings the file back") {
    auto img = blankLinear(40);
    InitOptions one;
    one.segments = 1;
    initVolume(img, 0, false, one, Vol::linear);
    const std::vector<uint8_t> a(2 * kBlock, 1), b(3 * kBlock, 2), c(kBlock, 3);
    PutOptions dated;
    dated.date = encodeDate(1992, 8, 22);
    putFile(img, 0, false, "A.DAT", a, dated, Vol::linear);
    putFile(img, 0, false, "B.DAT", b, dated, Vol::linear);
    putFile(img, 0, false, "C.DAT", c, {}, Vol::linear);

    removeFile(img, 0, false, "B.DAT", Vol::linear);
    auto im = openLinearImage(img);
    REQUIRE(im.has_value());
    REQUIRE(im->directory.entries.size() >= 4);
    const auto &gone = im->directory.entries[1];
    CHECK(gone.isEmpty());
    CHECK(gone.name == "B.DAT");
    CHECK(gone.length == 3);
    CHECK(gone.date == dated.date);
    CHECK(im->directory.find("B.DAT") == nullptr);

    CHECK_THROWS(undeleteEntry(img, 0, false, 0, "", Vol::linear));    /* a permanent file */
    CHECK_THROWS(undeleteEntry(img, 0, false, 3, "", Vol::linear));    /* the tail: the sentinel name */
    CHECK_THROWS(undeleteEntry(img, 0, false, 9, "", Vol::linear));    /* no such entry */

    undeleteEntry(img, 0, false, 1, "", Vol::linear);
    im = openLinearImage(img);
    CHECK(im->directory.permanentFiles().size() == 3);
    CHECK(im->readFile("B.DAT") == b);
    CHECK(im->directory.entries[1].date == dated.date);

    /* A file put over the freed area renames what remains of it: no undelete. */
    removeFile(img, 0, false, "B.DAT", Vol::linear);
    putFile(img, 0, false, "D.DAT", c, {}, Vol::linear);            /* lands in the 3-block slot */
    im = openLinearImage(img);
    CHECK(im->directory.entries[1].name == "D.DAT");
    CHECK(im->directory.entries[2].isEmpty());
    CHECK(im->directory.entries[2].name != "B.DAT");
    CHECK_THROWS(undeleteEntry(img, 0, false, 2, "", Vol::linear));
    {   /* ... but given a name, the area is recovered as a file, whatever lies in it */
        auto copy = img;
        undeleteEntry(copy, 0, false, 2, "AREA.DAT", Vol::linear);
        auto im2 = openLinearImage(copy);
        REQUIRE(im2->directory.find("AREA.DAT") != nullptr);
        CHECK(im2->directory.find("AREA.DAT")->length == 2);
        CHECK(im2->readFile("AREA.DAT").size() == 2 * kBlock);
    }

    /* A name taken meanwhile is refused - and another name given brings
     * the file back under it. */
    removeFile(img, 0, false, "A.DAT", Vol::linear);
    putFile(img, 0, false, "A.DAT", b, {}, Vol::linear);            /* 3 blocks: not the 2-block slot */
    CHECK_THROWS(undeleteEntry(img, 0, false, 0, "", Vol::linear));
    CHECK_THROWS(undeleteEntry(img, 0, false, 0, "A.DAT", Vol::linear));
    CHECK_THROWS(undeleteEntry(img, 0, false, 0, "TOOLONGNAME.DAT", Vol::linear));
    undeleteEntry(img, 0, false, 0, "A1.DAT", Vol::linear);
    im = openLinearImage(img);
    CHECK(im->readFile("A1.DAT") == a);
    CHECK(im->readFile("A.DAT") == b);
    CHECK(im->directory.entries[0].date == dated.date);
}

TEST_CASE("squeeze on a floppy keeps every file's bytes (the LBNs are skewed, not linear)") {
    /* Files spanning many blocks, one removed from the middle, the rest
     * moved down: each must read back byte-exact - the move goes block by
     * block through the layout, where consecutive LBNs are not consecutive
     * bytes of the image. */
    auto img = blankImage(false);
    initVolume(img, 0, false);
    const std::vector<BuildFile> files = {
        {"A.DAT", pattern(23 * 512, 1)}, {"B.DAT", pattern(7 * 512 + 100, 2)},
        {"C.DAT", pattern(41 * 512, 3)}, {"D.DAT", pattern(3 * 512, 4)}, {"E.DAT", pattern(60 * 512 + 1, 5)},
    };
    for (const auto &f : files) putFile(img, 0, false, f.name, f.data);
    removeFile(img, 0, false, "B.DAT");
    removeFile(img, 0, false, "D.DAT");
    squeeze(img, 0, false);
    auto opened = openImage(img, 0);
    REQUIRE(opened);
    int expectStart = opened->directory.dataStart;
    for (const auto &f : files) {
        if (f.name == "B.DAT" || f.name == "D.DAT") { CHECK(opened->directory.find(f.name) == nullptr); continue; }
        const DirEntry *e = opened->directory.find(f.name);
        REQUIRE(e != nullptr);
        CHECK(e->startBlock == expectStart);                     /* packed to the front, in order */
        expectStart += e->length;
        auto data = opened->readFile(f.name);
        REQUIRE(data.size() >= f.data.size());
        CHECK(std::equal(f.data.begin(), f.data.end(), data.begin()));
    }
    int empties = 0;
    for (const auto &e : opened->directory.entries) if (e.isEmpty() && e.length > 0) ++empties;
    CHECK(empties == 1);
}


TEST_CASE("dv/mz whole-disk volumes: init + put + read back per the handler formulas") {
    for (const Vol vol : {Vol::dv, Vol::mz}) {
        CAPTURE(static_cast<int>(vol));
        auto img = blankImage(true);
        initVolume(img, 0, true, {}, vol);

        /* the home block sits where the handler's translate puts LBN 1 */
        const std::size_t home = lbnToByte(1, 0, true, vol);
        CHECK(std::string(reinterpret_cast<const char *>(&img[home + 0x1F0]), 8)
              == "DECRT11A");

        const std::vector<uint8_t> one(3 * kBlock, 0x42);
        putFile(img, 0, true, "A.DAT", one, {}, vol);
        auto im = openVolume(img, vol);
        REQUIRE(im);
        REQUIRE(im->hasDirectory);
        CHECK(im->vol == vol);
        CHECK(im->readFile("A.DAT") == one);

        /* content-based detection names the format - uniquely */
        const auto specs = detectVolumes(img);
        REQUIRE(specs.size() == 1);
        CHECK(specs[0].vol == vol);

        /* first data block (LBN 14): byte-linear, DV rotated 20 blocks -
         * the offsets the OS oracle measured on the emulator */
        const std::size_t off = lbnToByte(14, 0, true, vol);
        CHECK(off == static_cast<std::size_t>(14 + (vol == Vol::dv ? 20 : 0)) * kBlock);
        CHECK(img[off] == 0x42);
    }
}

TEST_CASE("detectVolumes tells DZ sides, DV, MZ and linear apart by content") {
    auto dz = blankImage(true);              /* DZ on both sides of a DS floppy */
    initVolume(dz, 0, true);
    initVolume(dz, 1, true);
    auto specs = detectVolumes(dz);
    REQUIRE(specs.size() == 2);
    CHECK(specs[0].vol == Vol::floppy); CHECK(specs[0].side == 0);
    CHECK(specs[1].vol == Vol::floppy); CHECK(specs[1].side == 1);

    CHECK(detectVolumes(blankImage(true)).empty());   /* blanks carry nothing */
    CHECK(detectVolumes(blankImage(false)).empty());

    auto ss = blankImage(false);                          /* an SS floppy */
    initVolume(ss, 0, false);
    specs = detectVolumes(ss);
    REQUIRE(specs.size() == 1);
    CHECK(specs[0].vol == Vol::floppy);
    CHECK(specs[0].side == 0);

    auto hd = blankLinear(1000);              /* a linear container, odd size */
    initVolume(hd, 0, false, {}, Vol::linear);
    specs = detectVolumes(hd);
    REQUIRE(specs.size() == 1);
    CHECK(specs[0].vol == Vol::linear);
}
