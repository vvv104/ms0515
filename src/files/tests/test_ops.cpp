/*
 * test_ops.cpp — copy, move, delete, rename, protect between volumes, and
 * the host ends F1 / F2.
 */
#include "Ops.hpp"
#include "scratch.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ms0515::files;
namespace disk = ms0515::disk;

namespace {

Location openSide(const fs::path &image, int side, const std::string &dev)
{
    auto loc = Location::open(Device{dev, image, disk::VolumeSpec{disk::Vol::floppy, side}});
    REQUIRE(loc.has_value());
    return *loc;
}

std::vector<Entry> pick(const Location &loc, const std::vector<std::string> &names)
{
    std::vector<Entry> out;
    for (const auto &n : names) {
        const auto e = loc.find(n);
        REQUIRE(e.has_value());
        out.push_back(*e);
    }
    return out;
}

std::vector<uint8_t> bytesOf(const std::string &s) { return {s.begin(), s.end()}; }

} // namespace

TEST_CASE("copy between volumes keeps bytes, date and the [P] flag; a clash needs the overwrite policy")
{
    Scratch s("copy");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    const Location from = openSide(osa, 0, "DZ0:");
    Location to = openSide(rod, 1, "DZ3:");
    const auto sel = pick(from, {"DIR.SAV", "PIP.SAV"});

    const auto r = copyFiles(from, sel, to, {});
    CHECK(r.ok());
    CHECK(r.done == 2);
    const auto dir = to.find("DIR.SAV");
    REQUIRE(dir.has_value());
    CHECK(dir->date == "1990-12-27");
    CHECK(dir->protectedFlag);
    CHECK(*to.read("DIR.SAV") == *from.read("DIR.SAV"));
    CHECK_FALSE(to.find("PIP.SAV")->protectedFlag);

    /* the same again: refused without overwrite, done with it */
    const auto again = copyFiles(from, sel, to, {});
    CHECK(again.done == 0);
    CHECK(again.errors.size() == 2);
    CHECK(clashes(sel, to) == std::vector<std::string>{"DIR.SAV", "PIP.SAV"});
    Policy over;
    over.overwrite = true;
    over.touchProtected = true;           /* DIR.SAV on the target is [P] */
    CHECK(copyFiles(from, sel, to, over).ok());
    /* a protected target without the leave: refused */
    Policy noTouch;
    noTouch.overwrite = true;
    const auto kept = copyFiles(from, pick(from, {"DIR.SAV"}), to, noTouch);
    CHECK(kept.done == 0);
    CHECK(kept.errors.size() == 1);
}

TEST_CASE("move copies then removes the originals; delete honours [P]; rename checks the name")
{
    Scratch s("move");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    Location from = openSide(osa, 0, "DZ0:");
    Location to = openSide(rod, 0, "DZ1:");

    Policy p;
    p.touchProtected = true;
    p.overwrite = true;                   /* Rodionov's side has a DIR.SAV of its own */
    const auto moved = moveFiles(from, pick(from, {"DIR.SAV"}), to, p);
    const std::string movedWhy = moved.errors.empty() ? "" : moved.errors[0];
    CHECK_MESSAGE(moved.ok(), movedWhy);
    CHECK_FALSE(from.find("DIR.SAV").has_value());
    CHECK(to.find("DIR.SAV").has_value());

    /* SWAP.SYS is [P]: not deleted without the leave */
    CHECK(protectedOnes(pick(from, {"SWAP.SYS", "PIP.SAV"})) == std::vector<std::string>{"SWAP.SYS"});
    const auto refused = deleteFiles(from, pick(from, {"SWAP.SYS"}), {});
    CHECK(refused.done == 0);
    CHECK(from.find("SWAP.SYS").has_value());
    CHECK(deleteFiles(from, pick(from, {"SWAP.SYS", "PIP.SAV"}), p).done == 2);
    CHECK_FALSE(from.find("PIP.SAV").has_value());

    CHECK_FALSE(renameFile(to, *to.find("DIR.SAV"), "DIRX.SAV", {}).ok());       /* it is [P]: needs the leave */
    CHECK(renameFile(to, *to.find("DIR.SAV"), "DIRX.SAV", p).ok());
    CHECK(to.find("DIRX.SAV").has_value());
    CHECK_FALSE(renameFile(to, *to.find("DIRX.SAV"), "not a name", p).ok());
    CHECK_FALSE(renameFile(to, *to.find("DIRX.SAV"), "DIRX.SAV", p).ok());   /* onto itself */

    CHECK(protectFiles(to, pick(to, {"DIRX.SAV"}), false).ok());
    CHECK_FALSE(to.find("DIRX.SAV")->protectedFlag);
    CHECK(protectFiles(to, pick(to, {"DIRX.SAV"}), true).ok());
    CHECK(to.find("DIRX.SAV")->protectedFlag);
}

TEST_CASE("F1 brings host files onto the volume under RT-11 names with the policy's date; F2 puts files back")
{
    Scratch s("host");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    Location vol = openSide(osa, 0, "DZ0:");
    const auto note = s.text("my-notes.txt", "some notes");
    const auto readme = s.text("readme", "read me");

    Policy p;
    p.date = "1994-02-18";
    const auto in = importFiles({note, readme, s.dir() / "missing.txt"}, vol, p);
    CHECK(in.done == 2);
    CHECK(in.errors.size() == 1);
    REQUIRE(vol.find("MYNOTE.TXT").has_value());
    CHECK(vol.find("MYNOTE.TXT")->date == "1994-02-18");
    CHECK(vol.find("README").has_value());
    const auto got = vol.read("MYNOTE.TXT");
    REQUIRE(got.has_value());
    CHECK(std::string(got->begin(), got->begin() + 10) == "some notes");

    const auto outDir = s.dir() / "out" / "deeper";
    const auto out = exportFiles(vol, pick(vol, {"MYNOTE.TXT", "DIR.SAV"}), outDir, {});
    CHECK(out.ok());
    CHECK(out.done == 2);
    CHECK(fs::exists(outDir / "MYNOTE.TXT"));
    CHECK(fs::file_size(outDir / "DIR.SAV") == 20 * 512);
    /* the same again clashes on the host too */
    CHECK(exportFiles(vol, pick(vol, {"DIR.SAV"}), outDir, {}).done == 0);
    Policy over;
    over.overwrite = true;
    CHECK(exportFiles(vol, pick(vol, {"DIR.SAV"}), outDir, over).done == 1);
}
