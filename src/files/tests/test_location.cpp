/*
 * test_location.cpp — a device's volume: the listing, reading, and the
 * changes that must land in the image file.
 */
#include "Location.hpp"
#include "scratch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace ms0515::files;
namespace disk = ms0515::disk;

namespace {

Device dz0(const fs::path &image, int side = 0)
{
    return Device{side == 0 ? "DZ0:" : "DZ2:", image, disk::VolumeSpec{disk::Vol::floppy, side}};
}

std::vector<uint8_t> bytesOf(const std::string &s) { return {s.begin(), s.end()}; }

} // namespace

TEST_CASE("a single-sided image lists its files by name, with blocks, date and the [P] flag")
{
    Scratch s("list");
    const auto path = s.disk("test_osa.dsk", "osa.dsk");
    const auto vol = Location::open(dz0(path));
    REQUIRE(vol.has_value());
    CHECK(vol->hasDirectory());
    CHECK(vol->title() == "DZ0: osa.dsk");
    CHECK(vol->device().label() == "DZ0: osa.dsk");

    const auto entries = vol->list();
    REQUIRE(entries.size() >= 9);
    /* the directory's own order: by offset, the unused areas in their places */
    CHECK(std::is_sorted(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) { return a.offset < b.offset; }));
    const auto dir = vol->find("DIR.SAV");
    REQUIRE(dir.has_value());
    CHECK(dir->blocks == 20);
    CHECK(dir->bytes == 20 * 512);
    CHECK(dir->date == "1990-12-27");
    CHECK(dir->protectedFlag);
    CHECK_FALSE(vol->find("PIP.SAV")->protectedFlag);
    CHECK_FALSE(vol->find("NOSUCH.XXX").has_value());
    CHECK(vol->summary().find("free") != std::string::npos);
}

TEST_CASE("a two-sided image is two volumes, one per side")
{
    Scratch s("ds");
    const auto path = s.disk("test_rod.dsk", "rod.dsk");
    const auto side0 = Location::open(dz0(path, 0));
    const auto side1 = Location::open(dz0(path, 1));
    REQUIRE(side0.has_value());
    REQUIRE(side1.has_value());
    CHECK(side1->title() == "DZ2: rod.dsk");
    CHECK(side0->hasDirectory());
    CHECK_FALSE(side0->list().empty());
    /* the fixture's upper side is an initialised, empty volume */
    CHECK(side1->hasDirectory());
    REQUIRE(side1->list().size() == 1);   /* the free space INIT left, and nothing else */
    CHECK(side1->list()[0].empty);
    CHECK(side1->list()[0].name.empty());
    CHECK(side1->summary().find("0 files") != std::string::npos);
}

TEST_CASE("reading a file gives the bytes the disk library reads; meta comes from the entry")
{
    Scratch s("read");
    const auto path = s.disk("test_osa.dsk", "osa.dsk");
    const auto vol = Location::open(dz0(path));
    REQUIRE(vol.has_value());
    const auto bytes = vol->read("DIR.SAV");
    REQUIRE(bytes.has_value());
    const auto img = disk::loadImage(path.string(), 0);
    REQUIRE(img.has_value());
    CHECK(*bytes == img->readFile("DIR.SAV"));
    CHECK_FALSE(vol->read("NOSUCH.XXX").has_value());
}

TEST_CASE("writing, renaming, dating, protecting and removing persist in the image file")
{
    Scratch s("write");
    const auto path = s.disk("test_osa.dsk", "osa.dsk");
    auto vol = Location::open(dz0(path));
    REQUIRE(vol.has_value());

    CHECK(vol->write("HELLO.TXT", bytesOf("hello, volume"), FileMeta{"1995-04-01", false}).empty());
    auto hello = vol->find("HELLO.TXT");
    REQUIRE(hello.has_value());
    CHECK(hello->blocks == 1);
    CHECK(hello->date == "1995-04-01");
    CHECK_FALSE(hello->protectedFlag);
    CHECK_FALSE(vol->write("bad name", bytesOf("x"), {}).empty());   /* not an RT-11 name */

    /* a fresh Location on the same file sees it: the image was saved */
    {
        const auto again = Location::open(dz0(path));
        REQUIRE(again.has_value());
        const auto got = again->read("HELLO.TXT");
        REQUIRE(got.has_value());
        CHECK(std::string(got->begin(), got->begin() + 13) == "hello, volume");
    }

    CHECK(vol->rename("HELLO.TXT", "HI.TXT").empty());
    CHECK_FALSE(vol->find("HELLO.TXT").has_value());
    REQUIRE(vol->find("HI.TXT").has_value());
    CHECK_FALSE(vol->rename("HI.TXT", "DIR.SAV").empty());         /* taken */

    CHECK(vol->setProtected("HI.TXT", true).empty());
    CHECK(vol->find("HI.TXT")->protectedFlag);
    CHECK(vol->setProtected("HI.TXT", false).empty());
    CHECK_FALSE(vol->find("HI.TXT")->protectedFlag);
    CHECK(vol->setDate("HI.TXT", "1993-06-21").empty());
    CHECK(vol->find("HI.TXT")->date == "1993-06-21");

    CHECK_FALSE(vol->remove("NOSUCH.XXX").empty());
    CHECK(vol->remove("HI.TXT").empty());
    CHECK_FALSE(vol->find("HI.TXT").has_value());
    CHECK(vol->squeeze().empty());
    CHECK(Location::open(dz0(path))->read("DIR.SAV").has_value());   /* still a valid volume */
    CHECK(vol->reload());
}

TEST_CASE("a blank image opens as a volume without a directory, and init() gives it one")
{
    Scratch s("init");
    const auto path = s.file("blank.dsk", disk::blankImage(false));
    auto vol = Location::open(dz0(path));
    REQUIRE(vol.has_value());
    CHECK_FALSE(vol->hasDirectory());
    CHECK(vol->list().empty());
    CHECK(vol->summary().find("no RT-11 directory") != std::string::npos);
    CHECK_FALSE(vol->write("A.TXT", bytesOf("a"), {}).empty());
    disk::InitOptions opts;
    opts.volumeId = "SCRATCH";
    CHECK(vol->init(opts).empty());
    CHECK(vol->hasDirectory());
    CHECK(vol->volumeId().find("SCRATCH") != std::string::npos);
    CHECK(vol->write("A.TXT", bytesOf("a"), {}).empty());
    CHECK(Location::open(dz0(path))->find("A.TXT").has_value());
}

TEST_CASE("the host end: a whole file read and written")
{
    Scratch s("host");
    const auto p = s.text("note.txt", "abc");
    const auto got = readHostFile(p);
    REQUIRE(got.has_value());
    CHECK(*got == bytesOf("abc"));
    CHECK_FALSE(readHostFile(s.dir() / "missing.txt").has_value());
    CHECK(writeHostFile(s.dir() / "out.bin", bytesOf("xyz")).empty());
    CHECK(*readHostFile(s.dir() / "out.bin") == bytesOf("xyz"));
    CHECK_FALSE(writeHostFile(s.dir() / "no-such-dir" / "x", bytesOf("x")).empty());
}

TEST_CASE("RT-11 names and dates")
{
    CHECK(Location::validName("DIR.SAV"));
    CHECK(Location::validName("A.B"));
    CHECK(Location::validName("NONAME"));
    CHECK(Location::validName("K13U$.SAV"));
    CHECK_FALSE(Location::validName("toolong.sav"));   /* seven letters */
    CHECK_FALSE(Location::validName("DIR.SAVE"));
    CHECK_FALSE(Location::validName("my-file.txt"));
    CHECK_FALSE(Location::validName(""));
    CHECK(Location::toVolumeName("my-file.txt") == "MYFILE.TXT");
    CHECK(Location::toVolumeName("readme") == "README");
    CHECK(Location::toVolumeName("a.very.long.name.markdown") == "AVERYL.MAR");
    CHECK(Location::toVolumeName("DIR.SAV") == "DIR.SAV");
    CHECK(Location::dateText(Location::dateWord("1990-12-27")) == "1990-12-27");
    CHECK(Location::dateWord("") == 0);
    CHECK(Location::dateWord("nonsense") == 0);
    CHECK(Location::dateText(0).empty());
}

TEST_CASE("the listing carries the offsets and the unused areas; a deleted file's area keeps its name and comes back with undelete()")
{
    Scratch s("unused");
    const auto path = s.disk("test_osa.dsk", "osa.dsk");
    auto vol = Location::open(dz0(path));
    REQUIRE(vol.has_value());
    const auto all = vol->list();
    /* the directory order: offsets ascend, files and the free areas alike */
    for (size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1].offset <= all[i].offset);
    const auto files = std::count_if(all.begin(), all.end(), [](const Entry &e) { return !e.empty; });
    const auto areas = std::count_if(all.begin(), all.end(), [](const Entry &e) { return e.empty; });
    CHECK(files >= 9);
    CHECK(areas >= 1);
    /* the tail of the volume is the free space INIT left, without a file's name */
    CHECK(all.back().empty);
    CHECK(all.back().name.empty());
    CHECK(all.back().blocks > 0);
    /* find() knows the files only */
    CHECK(vol->find("DIR.SAV").has_value());
    CHECK_FALSE(vol->find("DIR.SAV")->empty);
    CHECK(vol->find("DIR.SAV")->offset > 0);

    /* delete PIP.SAV: its area stays, named, at the same offset, and reads as the file did */
    const auto pip = *vol->find("PIP.SAV");
    const auto pipBytes = *vol->read("PIP.SAV");
    CHECK(vol->remove("PIP.SAV").empty());
    const auto after = vol->list();
    const auto area = std::find_if(after.begin(), after.end(), [](const Entry &e) { return e.empty && e.name == "PIP.SAV"; });
    REQUIRE(area != after.end());
    CHECK(area->offset == pip.offset);
    CHECK(area->blocks == pip.blocks);
    CHECK_FALSE(vol->find("PIP.SAV").has_value());
    const auto raw = vol->readArea(*area);
    REQUIRE(raw.has_value());
    CHECK(*raw == pipBytes);
    /* readArea() reads a file's blocks as well */
    CHECK(*vol->readArea(*vol->find("DIR.SAV")) == *vol->read("DIR.SAV"));

    /* back it comes, under its own name; the tail area is not a file to bring back */
    CHECK(vol->undelete(*area, "").empty());
    REQUIRE(vol->find("PIP.SAV").has_value());
    CHECK(*vol->read("PIP.SAV") == pipBytes);
    CHECK_FALSE(vol->undelete(vol->list().back(), "").empty());
    /* ... unless a name is given: then the free space becomes a file of that name */
    CHECK(vol->undelete(vol->list().back(), "REST.DAT").empty());
    CHECK(vol->find("REST.DAT").has_value());
    /* a file is not an area */
    CHECK_FALSE(vol->undelete(*vol->find("DIR.SAV"), "").empty());
}
