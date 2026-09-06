/*
 * test_panel.cpp — the cursor, the marks and the selection over a volume.
 */
#include "Panel.hpp"
#include "scratch.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ms0515::files;
namespace disk = ms0515::disk;

namespace {

Panel osaPanel(Scratch &s)
{
    const auto path = s.disk("test_osa.dsk", "osa.dsk");
    auto loc = Location::open(Device{"DZ0:", path, disk::VolumeSpec{disk::Vol::floppy, 0}});
    REQUIRE(loc.has_value());
    return Panel(*loc);
}

} // namespace

TEST_CASE("an empty panel has no location, no entries and a title saying so")
{
    Panel p;
    CHECK_FALSE(p.hasLocation());
    CHECK(p.entries().empty());
    CHECK_FALSE(p.current().has_value());
    CHECK(p.selection().empty());
    CHECK(p.title() == "no disk");
    p.moveCursor(1);          /* harmless */
    p.toggleMark();
    CHECK(p.markedCount() == 0);
}

TEST_CASE("the cursor moves within the listing and pages by the visible rows")
{
    Scratch s("cursor");
    Panel p = osaPanel(s);
    const int n = static_cast<int>(p.entries().size());
    REQUIRE(n >= 9);
    CHECK(p.title() == "DZ0: osa.dsk");
    CHECK(p.cursor() == 0);
    p.moveCursor(-1);
    CHECK(p.cursor() == 0);
    p.moveCursor(3);
    CHECK(p.cursor() == 3);
    CHECK(p.current()->name == p.entries()[3].name);
    p.end();
    CHECK(p.cursor() == n - 1);
    p.moveCursor(5);
    CHECK(p.cursor() == n - 1);
    p.home();
    CHECK(p.cursor() == 0);
    p.pageDown(4);
    CHECK(p.cursor() == 4);
    p.pageUp(4);
    CHECK(p.cursor() == 0);
    /* the scroll offset keeps the cursor on the visible rows */
    CHECK(p.scrollTop(4) == 0);
    p.end();
    CHECK(p.scrollTop(4) == n - 4);
    p.moveCursor(-3);
    CHECK(p.scrollTop(4) == n - 4);
}

TEST_CASE("marks: Insert toggles and steps down; the selection is the marks, else the cursor")
{
    Scratch s("marks");
    Panel p = osaPanel(s);
    const auto first = p.entries()[0].name;
    const auto second = p.entries()[1].name;
    CHECK(p.selection().size() == 1);
    CHECK(p.selection()[0].name == first);

    p.toggleMark();                       /* marks the first, moves to the second */
    CHECK(p.isMarked(first));
    CHECK(p.cursor() == 1);
    p.toggleMark();
    CHECK(p.markedCount() == 2);
    const auto sel = p.selection();
    REQUIRE(sel.size() == 2);
    CHECK(sel[0].name == first);
    CHECK(sel[1].name == second);

    p.moveCursor(-1);                     /* back on the second */
    p.toggleMark();                       /* unmarks it */
    CHECK_FALSE(p.isMarked(second));
    CHECK(p.markedCount() == 1);
    p.clearMarks();
    CHECK(p.markedCount() == 0);
}

TEST_CASE("reload keeps the cursor on its name and drops marks on names that vanished")
{
    Scratch s("reload");
    Panel p = osaPanel(s);
    p.moveCursor(2);
    const auto name = p.current()->name;
    p.toggleMark();                       /* marks `name`, cursor now on the next */
    p.moveCursor(-1);
    CHECK(p.location().write("ZZZ.TMP", std::vector<uint8_t>{1, 2, 3}, {}).empty());
    p.reload();
    CHECK(p.current()->name == name);
    CHECK(p.isMarked(name));
    CHECK(p.location().remove(name).empty());
    p.reload();
    CHECK_FALSE(p.isMarked(name));
    CHECK(p.markedCount() == 0);
    CHECK(p.cursor() < static_cast<int>(p.entries().size()));
}

TEST_CASE("show() switches the panel to another volume, clear() empties it")
{
    Scratch s("show");
    Panel p = osaPanel(s);
    p.moveCursor(2);
    p.toggleMark();
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    auto loc = Location::open(Device{"DZ2:", rod, disk::VolumeSpec{disk::Vol::floppy, 1}});
    REQUIRE(loc.has_value());
    p.show(*loc);
    CHECK(p.title() == "DZ2: rod.dsk");
    CHECK(p.cursor() == 0);
    CHECK(p.markedCount() == 0);
    p.clear();
    CHECK_FALSE(p.hasLocation());
    CHECK(p.entries().empty());
}
