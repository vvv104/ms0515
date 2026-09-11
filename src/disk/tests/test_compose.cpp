/*
 * test_compose.cpp - whole diskettes from an exemplar and groups of files:
 * the kit as the exemplar has it, the groups where the rules put them, the
 * bootstrap for the media, and the plan agreeing with what gets built.
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

}  /* namespace */

TEST_SUITE("Compose") {

TEST_CASE("mediaOf tells the three diskettes apart") {
    CHECK(mediaOf(exemplar(Media::ss)) == Media::ss);
    CHECK(mediaOf(exemplar(Media::dz)) == Media::dz);
    CHECK(mediaOf(exemplar(Media::dv)) == Media::dv);
    CHECK_FALSE(mediaOf(blankImage(true)).has_value());
    CHECK_FALSE(mediaOf(std::vector<uint8_t>(1000)).has_value());
}

TEST_CASE("every media from every exemplar: the kit as it was, then the groups, and it boots") {
    for (const Media from : {Media::ss, Media::dz, Media::dv})
    for (const Media to : {Media::ss, Media::dz, Media::dv}) {
        CAPTURE(static_cast<int>(from)); CAPTURE(static_cast<int>(to));
        const auto sys = exemplar(from);
        ComposeRecipe r;
        r.system = sys;
        r.media = to;
        r.groups = {{"games", Place::boot, {file("BIRDS.SAV", 4, 0x11), file("BIRDS.DAT", 2, 0x12)}}};
        const auto img = composeDisk(r);
        REQUIRE(mediaOf(img) == to);

        const auto src = volume(sys, from), got = volume(img, to);
        REQUIRE(got);
        auto want = kKit;
        want.push_back("BIRDS.SAV");
        want.push_back("BIRDS.DAT");
        CHECK(names(*got) == want);
        for (const auto &name : kKit) {
            CAPTURE(name);
            const auto *a = src->directory.find(name), *b = got->directory.find(name);
            REQUIRE(b);
            CHECK(b->date == a->date);
            CHECK((b->status & kStatusProtected) == (a->status & kStatusProtected));
            CHECK(got->readFile(name) == src->readFile(name));
        }
        CHECK(got->readFile("BIRDS.SAV") == std::vector<uint8_t>(4 * kBlock, 0x11));
        CHECK(got->directory.find("BIRDS.SAV")->date == encodeDate(1991, 11, 5));
        CHECK(bootedMonitor(img, 0, to != Media::ss, bootVol(to)) == "RT11SJ");
        if (to == Media::dz) {
            REQUIRE(volume(img, to, 1));
            CHECK(volume(img, to, 1)->directory.permanentFiles().empty());
        }
    }
}

TEST_CASE("the startup file: the exemplar's kept, or made of the lines given") {
    ComposeRecipe r;
    r.system = exemplar(Media::dv);
    r.media = Media::ss;
    auto kept = volume(composeDisk(r), Media::ss)->readFile("START.COM");
    CHECK(std::string(kept.begin(), kept.begin() + 25) == "SET TT QUIET\r\nSET SL ON\r\n");

    r.startup = std::vector<std::string>{"SET TT QUIET", "LOAD VM:", "R ROSA3"};
    const auto img = composeDisk(r);
    const auto im = volume(img, Media::ss);
    const auto made = im->readFile("START.COM");
    CHECK(std::string(made.begin(), made.begin() + 33) == "SET TT QUIET\r\nLOAD VM:\r\nR ROSA3\r\n");
    CHECK(made[33] == 0);
    CHECK(names(*im) == kKit);                                /* in its place, not appended */
    CHECK((im->directory.find("START.COM")->status & kStatusProtected) == 0);
}

TEST_CASE("labels: the boot volume's and the second's") {
    ComposeRecipe r;
    r.system = exemplar(Media::dv);
    r.media = Media::dz;
    r.volumeId = "GAMES";
    r.owner = "MS0515 EMU";
    r.secondVolumeId = "TEXT GAMES";
    const auto img = composeDisk(r);
    CHECK(homeField(img, Media::dz, 0, 0x1D8) == "GAMES       ");
    CHECK(homeField(img, Media::dz, 0, 0x1E4) == "MS0515 EMU  ");
    CHECK(homeField(img, Media::dz, 1, 0x1D8) == "TEXT GAMES  ");
}

TEST_CASE("a group that may go anywhere goes to the second volume when the boot one is full, whole") {
    ComposeRecipe r;
    r.system = exemplar(Media::dz);
    r.media = Media::dz;
    const auto plan0 = planDisk(r);
    REQUIRE(plan0.ok);
    REQUIRE(plan0.freeBlocks.size() == 2);
    const int bootFree = plan0.freeBlocks[0];

    r.groups = {
        {"big", Place::boot, {file("BIG.SAV", bootFree - 3, 0x21)}},
        {"pair", Place::any, {file("ONE.SAV", 2, 0x22), file("TWO.DAT", 2, 0x23)}},   /* the first would fit */
        {"small", Place::any, {file("TINY.SAV", 3, 0x24)}},                          /* this one fits */
    };
    const auto plan = planDisk(r);
    REQUIRE(plan.ok);
    REQUIRE(plan.groups.size() == 3);
    CHECK(plan.groups[0].volume == 0);
    CHECK(plan.groups[1].volume == 1);
    CHECK(plan.groups[1].blocks == 4);
    CHECK(plan.groups[2].volume == 0);
    CHECK(plan.freeBlocks[0] == 0);
    CHECK(plan.freeBlocks[1] == plan0.freeBlocks[1] - 4);

    const auto img = composeDisk(r);
    CHECK(names(*volume(img, Media::dz, 1)) == std::vector<std::string>{"ONE.SAV", "TWO.DAT"});
    CHECK(volume(img, Media::dz, 0)->directory.find("TINY.SAV") != nullptr);
}

TEST_CASE("what cannot be built says why, and the plan still shows what fitted") {
    ComposeRecipe r;
    r.system = exemplar(Media::dv);
    r.media = Media::ss;
    const int bootFree = planDisk(r).freeBlocks.at(0);

    SUBCASE("a boot group too big for the boot volume") {
        r.groups = {{"ok", Place::boot, {file("A.SAV", 2, 1)}},
                    {"Saboteur 2", Place::boot, {file("SABOT2.DAT", bootFree, 2)}}};
        const auto plan = planDisk(r);
        CHECK_FALSE(plan.ok);
        CHECK(plan.problem.find("Saboteur 2") != std::string::npos);
        CHECK(plan.groups[0].volume == 0);
        CHECK(plan.groups[1].volume == -1);
        CHECK_FALSE(plan.groups[1].problem.empty());
        CHECK_THROWS_WITH_AS((void)composeDisk(r), doctest::Contains("Saboteur 2"), std::runtime_error);
    }
    SUBCASE("an any group with no second volume to go to") {
        r.groups = {{"docs", Place::any, {file("DOC.TXT", bootFree + 1, 3)}}};
        CHECK_FALSE(planDisk(r).ok);
    }
    SUBCASE("a name already on the volume") {
        r.groups = {{"mine", Place::boot, {file("PIP.SAV", 1, 4)}}};
        const auto plan = planDisk(r);
        CHECK_FALSE(plan.ok);
        CHECK(plan.problem.find("PIP.SAV") != std::string::npos);
    }
    SUBCASE("a name that is no 6.3 name") {
        r.groups = {{"bad", Place::boot, {file("TOOLONGNAME.SAV", 1, 5)}}};
        CHECK_FALSE(planDisk(r).ok);
    }
    SUBCASE("a DV disk from an exemplar that has no DV.SYS") {
        auto sys = exemplar(Media::ss);
        removeFile(sys, 0, false, "DV.SYS");
        r.system = sys;
        r.media = Media::dv;
        CHECK_FALSE(planDisk(r).ok);
    }
    SUBCASE("an exemplar that does not boot") {
        auto sys = blankImage(false);
        initVolume(sys, 0, false);
        r.system = sys;
        CHECK_FALSE(planDisk(r).ok);
    }
}

TEST_CASE("an exemplar kept as it is: its media only, and its reserved blocks untouched") {
    auto sys = exemplar(Media::dz);
    const int protLbn = 792;                                   /* free as far as side 1's directory knows */
    const auto at = lbnToByte(protLbn, 1, true, Vol::floppy);
    for (std::size_t i = 0; i < kBlock; ++i) sys[at + i] = static_cast<uint8_t>(0x5A ^ i);

    ComposeRecipe r;
    r.system = sys;
    r.rebuild = false;
    r.media = Media::dz;
    r.reserved = {{1, protLbn}};
    r.startup = std::vector<std::string>{"LOAD VM:", "R ROSA3"};
    r.groups = {{"rosa", Place::boot, {file("ROSA3.SAV", 5, 0x31)}}};
    const auto img = composeDisk(r);
    CHECK(std::equal(img.begin() + static_cast<std::ptrdiff_t>(at), img.begin() + static_cast<std::ptrdiff_t>(at + kBlock),
                     sys.begin() + static_cast<std::ptrdiff_t>(at)));
    CHECK(bootedMonitor(img, 0, true) == "RT11SJ");
    CHECK(volume(img, Media::dz)->directory.find("ROSA3.SAV") != nullptr);

    const int sideFree = planDisk(r).freeBlocks.at(1);
    r.groups.push_back({"fills side 1", Place::any, {file("HUGE.DAT", sideFree, 0x32)}});
    const auto plan = planDisk(r);
    CHECK_FALSE(plan.ok);
    CHECK(plan.problem.find("reserved") != std::string::npos);

    r.groups.pop_back();
    r.media = Media::ss;
    CHECK_FALSE(planDisk(r).ok);                               /* not its own media */
}

TEST_CASE("the plan and the build agree: free blocks are what the directory says afterwards") {
    ComposeRecipe r;
    r.system = exemplar(Media::dv);
    r.media = Media::dz;
    r.groups = {{"a", Place::boot, {file("A.SAV", 7, 1)}}, {"b", Place::any, {file("B.DAT", 9, 2)}}};
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
