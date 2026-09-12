/*
 * test_compose.cpp - whole diskettes made from scratch: the exemplar gives
 * only its SWAP.SYS, its monitor and the blocks it protects; everything else
 * comes as groups of files; then the startup file and the bootstrap for the
 * media.  The plan agrees with what gets built.
 */

#include <doctest/doctest.h>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Image.hpp>

#include "exemplar_fixture.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ms0515::disk;
using namespace ms0515::disk::fixture;

namespace {

ComposeFile file(const std::string &name, int blocks, uint8_t fill)
{
    return {name, std::vector<uint8_t>(static_cast<std::size_t>(blocks) * kBlock, fill), encodeDate(1991, 11, 5), false};
}

Vol bootVol(Media m) { return m == Media::dv ? Vol::dv : Vol::floppy; }

std::optional<Image> volume(const std::vector<uint8_t> &img, Media m, int side = 0)
{
    return openVolume(img, side == 0 ? bootVol(m) : Vol::floppy, side);
}

std::vector<std::string> names(const Image &im)
{
    std::vector<std::string> out;
    for (const auto &e : im.directory.permanentFiles()) out.push_back(e.name);
    return out;
}

std::string homeField(const std::vector<uint8_t> &img, Media m, int side, int off)
{
    const auto at = lbnToByte(1, side, m != Media::ss, side == 0 ? bootVol(m) : Vol::floppy);
    return std::string(reinterpret_cast<const char *>(img.data()) + at + off, 12);
}

/* The system's parts as the manifest would bring them: files read off the
 * exemplar here, the way the collection holds the same bytes loose. */
ComposeGroup parts(const std::vector<uint8_t> &exemplarImage, Media from, std::vector<std::string> wanted)
{
    const auto src = volume(exemplarImage, from);
    ComposeGroup g{"system", Place::boot, {}};
    for (const auto &name : wanted) {
        const auto *e = src->directory.find(name);
        REQUIRE_MESSAGE(e, name);
        g.files.push_back({name, src->readFile(name), e->date, (e->status & kStatusProtected) != 0});
    }
    return g;
}

ComposeRecipe recipe(Media from, Media to)
{
    ComposeRecipe r;
    r.system = exemplar(from);
    r.media = to;
    std::vector<std::string> wanted{"DZ.SYS", "TT.SYS", "PIP.SAV"};
    if (to == Media::dv) wanted.insert(wanted.begin(), "DV.SYS");
    r.groups = {parts(r.system, from, wanted)};
    return r;
}

}  /* namespace */

TEST_SUITE("Compose") {

TEST_CASE("mediaOf tells the three diskettes apart") {
    CHECK(mediaOf(exemplar(Media::ss)) == Media::ss);
    CHECK(mediaOf(exemplar(Media::dz)) == Media::dz);
    CHECK(mediaOf(exemplar(Media::dv)) == Media::dv);
    CHECK_FALSE(mediaOf(blankImage(true)).has_value());
    CHECK_FALSE(mediaOf(std::vector<uint8_t>(1000)).has_value());

    /* A single side whose home block does not point at its directory (older
     * tools wrote it so) is still one volume: a side has no other reading. */
    auto old = exemplar(Media::ss);
    const auto home = lbnToByte(1, 0, false, Vol::floppy);
    old[home + 0x1D4] = 0;
    old[home + 0x1D5] = 0;
    CHECK(mediaOf(old) == Media::ss);
    CHECK_FALSE(mediaOf(blankImage(false)).has_value());
}

TEST_CASE("every media from every exemplar: SWAP and the monitor from it, the parts given, and it boots") {
    for (const Media from : {Media::ss, Media::dz, Media::dv})
    for (const Media to : {Media::ss, Media::dz, Media::dv}) {
        CAPTURE(static_cast<int>(from)); CAPTURE(static_cast<int>(to));
        ComposeRecipe r = recipe(from, to);
        r.groups.push_back({"games", Place::boot, {file("BIRDS.SAV", 4, 0x11), file("BIRDS.DAT", 2, 0x12)}});
        const auto img = composeDisk(r);
        REQUIRE(mediaOf(img) == to);

        const auto src = volume(r.system, from), got = volume(img, to);
        REQUIRE(got);
        std::vector<std::string> want{"SWAP.SYS", "RT11SJ.SYS"};
        if (to == Media::dv) want.push_back("DV.SYS");
        for (const char *n : {"DZ.SYS", "TT.SYS", "PIP.SAV", "BIRDS.SAV", "BIRDS.DAT", "START.COM"}) want.push_back(n);
        CHECK(names(*got) == want);                          /* SL.SYS is on the exemplar and not taken */
        for (const char *name : {"SWAP.SYS", "RT11SJ.SYS", "DZ.SYS", "START.COM"}) {
            CAPTURE(name);
            const auto *a = src->directory.find(name), *b = got->directory.find(name);
            REQUIRE(b);
            CHECK(b->date == a->date);
            CHECK((b->status & kStatusProtected) == (a->status & kStatusProtected));
            CHECK(got->readFile(name) == src->readFile(name));
        }
        CHECK(got->readFile("BIRDS.SAV") == std::vector<uint8_t>(4 * kBlock, 0x11));
        CHECK(bootedMonitor(img, 0, to != Media::ss, bootVol(to)) == "RT11SJ");
        if (to == Media::dz) {
            REQUIRE(volume(img, to, 1));
            CHECK(volume(img, to, 1)->directory.permanentFiles().empty());
        }
    }
}

TEST_CASE("the startup file: the exemplar's copied, or made of the lines given") {
    ComposeRecipe r = recipe(Media::dv, Media::ss);
    auto kept = volume(composeDisk(r), Media::ss)->readFile("START.COM");
    CHECK(std::string(kept.begin(), kept.begin() + 25) == "SET TT QUIET\r\nSET SL ON\r\n");

    r.startup = std::vector<std::string>{"SET TT QUIET", "LOAD VM:", "R ROSA3"};
    const auto im = volume(composeDisk(r), Media::ss);
    const auto made = im->readFile("START.COM");
    CHECK(std::string(made.begin(), made.begin() + 33) == "SET TT QUIET\r\nLOAD VM:\r\nR ROSA3\r\n");
    CHECK(made[33] == 0);
    CHECK((im->directory.find("START.COM")->status & kStatusProtected) == 0);
}

TEST_CASE("a disk that could not boot is refused: the handler of its boot device is missing") {
    ComposeRecipe r = recipe(Media::dv, Media::dv);
    r.groups = {parts(r.system, Media::dv, {"DZ.SYS", "TT.SYS"})};      /* no DV.SYS for a DV disk */
    auto plan = planDisk(r);
    CHECK_FALSE(plan.ok);
    CHECK(plan.problem.find("DV.SYS") != std::string::npos);

    r.media = Media::dz;
    CHECK(planDisk(r).ok);                                              /* a DZ disk needs only DZ.SYS */
    r.groups = {parts(r.system, Media::dv, {"TT.SYS"})};
    plan = planDisk(r);
    CHECK_FALSE(plan.ok);
    CHECK(plan.problem.find("DZ.SYS") != std::string::npos);
}

TEST_CASE("labels: the boot volume's and the second's") {
    ComposeRecipe r = recipe(Media::dv, Media::dz);
    r.volumeId = "GAMES";
    r.owner = "MS0515 EMU";
    r.secondVolumeId = "TEXT GAMES";
    const auto img = composeDisk(r);
    CHECK(homeField(img, Media::dz, 0, 0x1D8) == "GAMES       ");
    CHECK(homeField(img, Media::dz, 0, 0x1E4) == "MS0515 EMU  ");
    CHECK(homeField(img, Media::dz, 1, 0x1D8) == "TEXT GAMES  ");
}

TEST_CASE("a group that may go anywhere goes to the second volume when the boot one is full, whole") {
    ComposeRecipe r = recipe(Media::dz, Media::dz);
    const auto plan0 = planDisk(r);
    REQUIRE(plan0.ok);
    REQUIRE(plan0.freeBlocks.size() == 2);
    const int bootFree = plan0.freeBlocks[0];

    r.groups.push_back({"big", Place::boot, {file("BIG.SAV", bootFree - 2, 0x21)}});
    r.groups.push_back({"pair", Place::any, {file("ONE.SAV", 2, 0x22), file("TWO.DAT", 2, 0x23)}});
    r.groups.push_back({"small", Place::any, {file("TINY.SAV", 2, 0x24)}});
    const auto plan = planDisk(r);
    REQUIRE_MESSAGE(plan.ok, plan.problem);
    REQUIRE(plan.groups.size() == 4);
    CHECK(plan.groups[1].volume == 0);
    CHECK(plan.groups[2].volume == 1);
    CHECK(plan.groups[2].blocks == 4);
    CHECK(plan.groups[3].volume == 0);
    CHECK(plan.freeBlocks[1] == plan0.freeBlocks[1] - 4);

    const auto img = composeDisk(r);
    CHECK(names(*volume(img, Media::dz, 1)) == std::vector<std::string>{"ONE.SAV", "TWO.DAT"});
    CHECK(volume(img, Media::dz, 0)->directory.find("TINY.SAV") != nullptr);
}

TEST_CASE("the boot volume taken by what may go anywhere, and a group that must boot short of room: those move to the second volume") {
    ComposeRecipe r = recipe(Media::dz, Media::dz);
    const auto plan0 = planDisk(r);
    REQUIRE(plan0.ok);
    const int bootFree = plan0.freeBlocks[0];

    r.groups.push_back({"docs", Place::any, {file("DOCS.TXT", bootFree - 4, 0x31)}});
    r.groups.push_back({"handler", Place::boot, {file("NL.SYS", 8, 0x32)}});
    const auto plan = planDisk(r);
    REQUIRE_MESSAGE(plan.ok, plan.problem);
    REQUIRE(plan.groups.size() == 3);
    CHECK(plan.groups[1].title == "docs");                    /* the recipe's order kept in the plan */
    CHECK(plan.groups[1].volume == 1);
    CHECK(plan.groups[2].volume == 0);
    const auto img = composeDisk(r);
    CHECK(volume(img, Media::dz, 0)->directory.find("NL.SYS") != nullptr);
    CHECK(volume(img, Media::dz, 1)->directory.find("DOCS.TXT") != nullptr);

    r.groups.push_back({"too much", Place::boot, {file("HUGE.SAV", bootFree, 0x33)}});
    CHECK_FALSE(planDisk(r).ok);                              /* what must boot and does not fit, still refused */
}

TEST_CASE("what cannot be built says why, and the plan still shows what fitted") {
    ComposeRecipe r = recipe(Media::dv, Media::ss);
    const int bootFree = planDisk(r).freeBlocks.at(0);

    SUBCASE("a boot group too big for the boot volume") {
        r.groups.push_back({"ok", Place::boot, {file("A.SAV", 2, 1)}});
        r.groups.push_back({"Saboteur 2", Place::boot, {file("SABOT2.DAT", bootFree, 2)}});
        const auto plan = planDisk(r);
        CHECK_FALSE(plan.ok);
        CHECK(plan.problem.find("Saboteur 2") != std::string::npos);
        CHECK(plan.groups[1].volume == 0);
        CHECK(plan.groups[2].volume == -1);
        CHECK_FALSE(plan.groups[2].problem.empty());
        CHECK_THROWS_WITH_AS((void)composeDisk(r), doctest::Contains("Saboteur 2"), std::runtime_error);
    }
    SUBCASE("an any group with no second volume to go to") {
        r.groups.push_back({"docs", Place::any, {file("DOC.TXT", bootFree + 1, 3)}});
        CHECK_FALSE(planDisk(r).ok);
    }
    SUBCASE("a name already on the volume") {
        r.groups.push_back({"mine", Place::boot, {file("SWAP.SYS", 1, 4)}});
        const auto plan = planDisk(r);
        CHECK_FALSE(plan.ok);
        CHECK(plan.problem.find("SWAP.SYS") != std::string::npos);
    }
    SUBCASE("a name that is no 6.3 name") {
        r.groups.push_back({"bad", Place::boot, {file("TOOLONGNAME.SAV", 1, 5)}});
        CHECK_FALSE(planDisk(r).ok);
    }
    SUBCASE("an exemplar that does not boot") {
        auto sys = blankImage(false);
        initVolume(sys, 0, false);
        r.system = sys;
        CHECK_FALSE(planDisk(r).ok);
    }
}

/* The last empty entry of a volume's directory: where its free space ends. */
const DirEntry &freeTail(const Image &im)
{
    const auto &es = im.directory.entries;
    const auto it = std::find_if(es.rbegin(), es.rend(), [](const DirEntry &e) { return e.isEmpty(); });
    REQUIRE(it != es.rend());
    return *it;
}

TEST_CASE("reserved blocks: the exemplar's sectors on the same sectors of a dz or a dv disk, fenced off from the free space") {
    const int protLbn = 792;                                   /* side 1, physical track 0: Rodionov's */
    const auto at = lbnToByte(protLbn, 1, true, Vol::floppy);  /* the same bytes on either media */
    for (const Media to : {Media::dz, Media::dv}) {
        CAPTURE(static_cast<int>(to));
        ComposeRecipe r = recipe(Media::dz, to);
        for (std::size_t i = 0; i < kBlock; ++i) r.system[at + i] = static_cast<uint8_t>(0x5A ^ i);
        r.reserved = {{1, protLbn}};

        const auto img = composeDisk(r);
        CHECK(std::equal(img.begin() + static_cast<std::ptrdiff_t>(at), img.begin() + static_cast<std::ptrdiff_t>(at + kBlock),
                         r.system.begin() + static_cast<std::ptrdiff_t>(at)));
        CHECK(bootedMonitor(img, 0, true, bootVol(to)) == "RT11SJ");

        /* The volume's free space ends at the block: the last DZ blocks of
         * side 1, or DV block 1592 - the same sector, cylinder 0 being the
         * DV volume's last twenty blocks. */
        const int fenced = to == Media::dz ? protLbn : 1592;
        const int v = to == Media::dz ? 1 : 0;
        const auto vol = volume(img, to, v);
        REQUIRE(vol);
        const auto &tail = freeTail(*vol);
        CHECK(tail.startBlock + tail.length == fenced);
        for (const auto &f : vol->directory.permanentFiles()) CHECK(f.startBlock + f.length <= fenced);

        /* What is left takes the volume up to the block, and not past it:
         * one block more and either the group has no room, or START.COM
         * after it has none. */
        const int free = planDisk(r).freeBlocks.at(static_cast<std::size_t>(v));
        r.groups.push_back({"fills the volume", Place::any, {file("HUGE.DAT", free, 0x32)}});
        CHECK(planDisk(r).ok);
        r.groups.back().files[0] = file("HUGE.DAT", free + 1, 0x32);
        const auto plan = planDisk(r);
        CHECK_FALSE(plan.ok);
        CHECK((plan.problem.find("does not fit") != std::string::npos ||
               plan.problem.find("no free area") != std::string::npos));
    }
}

TEST_CASE("a reserved block on the second side has no place on a single-sided disk") {
    ComposeRecipe r = recipe(Media::dz, Media::ss);
    r.reserved = {{1, 792}};
    const auto plan = planDisk(r);
    CHECK_FALSE(plan.ok);
    CHECK(plan.problem.find("second side") != std::string::npos);
}

TEST_CASE("the plan and the build agree: free blocks are what the directory says afterwards") {
    ComposeRecipe r = recipe(Media::dv, Media::dz);
    r.groups.push_back({"a", Place::boot, {file("A.SAV", 7, 1)}});
    r.groups.push_back({"b", Place::any, {file("B.DAT", 9, 2)}});
    const auto plan = planDisk(r);
    const auto img = composeDisk(r);
    for (int side = 0; side < 2; ++side) {
        const auto im = volume(img, Media::dz, side);
        REQUIRE(im);
        int free = 0;
        for (const auto &e : im->directory.entries) if (e.isEmpty()) free += e.length;
        CHECK(plan.freeBlocks[static_cast<std::size_t>(side)] == free);
    }
}

}  /* TEST_SUITE */
