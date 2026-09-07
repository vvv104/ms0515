/*
 * test_panel.cpp — the cursor, the marks and the selection over a volume.
 */
#include "Panel.hpp"
#include "scratch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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
    p.setShowUnused(false);               /* files only here; the areas have their own case */
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

TEST_CASE("sort order: by name, extension, size, date, each reversible; the cursor stays on its file")
{
    Scratch s("sort");
    Panel p = osaPanel(s);
    p.setShowUnused(false);               /* the areas' place in the orders is the areas' case */
    p.home();
    const std::string first = p.current()->name;
    CHECK(p.sortOrder() == SortOrder::offset);

    p.setSort(SortOrder::size, false);
    const auto &bySize = p.entries();
    CHECK(std::is_sorted(bySize.begin(), bySize.end(), [](const Entry &a, const Entry &b) { return a.blocks < b.blocks; }));
    CHECK(p.current()->name == first);            /* the cursor follows its file */

    p.setSort(SortOrder::size, true);
    const auto &bySizeRev = p.entries();
    CHECK(std::is_sorted(bySizeRev.begin(), bySizeRev.end(), [](const Entry &a, const Entry &b) { return a.blocks > b.blocks; }));
    CHECK(p.reversed());

    p.setSort(SortOrder::extension, false);
    const auto &byExt = p.entries();
    CHECK(std::is_sorted(byExt.begin(), byExt.end(), [](const Entry &a, const Entry &b) {
        const auto ea = a.name.substr(a.name.find('.') + 1), eb = b.name.substr(b.name.find('.') + 1);
        return ea != eb ? ea < eb : a.name < b.name;
    }));

    p.setSort(SortOrder::date, false);
    const auto &byDate = p.entries();
    CHECK(std::is_sorted(byDate.begin(), byDate.end(), [](const Entry &a, const Entry &b) { return a.date < b.date; }));

    p.setSort(SortOrder::name, false);
    p.reload();                                   /* the order survives a reload */
    CHECK(std::is_sorted(p.entries().begin(), p.entries().end(), [](const Entry &a, const Entry &b) { return a.name < b.name; }));
}

TEST_CASE("marking by pattern: shell * and ?, case-insensitive; unmark; invert; the marked blocks add up")
{
    CHECK(matchPattern("DIR.SAV", "*.SAV"));
    CHECK(matchPattern("DIR.SAV", "*.sav"));
    CHECK(matchPattern("DIR.SAV", "D??.*"));
    CHECK(matchPattern("DIR.SAV", "*"));
    CHECK_FALSE(matchPattern("DIR.SAV", "*.SYS"));
    CHECK_FALSE(matchPattern("DIR.SAV", "DIR"));
    CHECK(matchPattern("SWAP.SYS", "S*S"));

    Scratch s("pattern");
    Panel p = osaPanel(s);
    const int savs = static_cast<int>(std::count_if(p.entries().begin(), p.entries().end(),
                                                    [](const Entry &e) { return e.name.ends_with(".SAV"); }));
    REQUIRE(savs >= 2);
    const int files = static_cast<int>(std::count_if(p.entries().begin(), p.entries().end(),
                                                     [](const Entry &e) { return !e.empty; }));
    p.markPattern("*.SAV", true);
    CHECK(p.markedCount() == savs);
    uint32_t blocks = 0;
    for (const auto &e : p.selection()) blocks += e.blocks;
    CHECK(p.markedBlocks() == blocks);
    p.markPattern("DIR.*", false);
    CHECK(p.markedCount() == savs - 1);
    CHECK_FALSE(p.isMarked("DIR.SAV"));
    p.invertMarks();
    CHECK(p.markedCount() == files - (savs - 1));
    CHECK(p.isMarked("DIR.SAV"));
    p.clearMarks();
    CHECK(p.markedBlocks() == 0);
}

TEST_CASE("the panel lists in directory order (by offset) with the unused areas; they take no marks; a setting hides them")
{
    Scratch s("areas");
    Panel p = osaPanel(s);
    CHECK(p.sortOrder() == SortOrder::offset);
    const auto &entries = p.entries();
    for (size_t i = 1; i < entries.size(); ++i) CHECK(entries[i - 1].offset <= entries[i].offset);
    CHECK(p.showUnused());
    const int withAreas = static_cast<int>(entries.size());
    const int areas = static_cast<int>(std::count_if(entries.begin(), entries.end(), [](const Entry &e) { return e.empty; }));
    REQUIRE(areas >= 1);

    /* the cursor on the tail area: no mark, no selection */
    p.end();
    REQUIRE(p.current()->empty);
    p.toggleMark();
    CHECK(p.markedCount() == 0);
    CHECK(p.selection().empty());
    p.markPattern("*", true);
    CHECK(p.markedCount() == withAreas - areas);
    p.clearMarks();

    /* hidden, like mc's hidden files */
    p.setShowUnused(false);
    CHECK_FALSE(p.showUnused());
    CHECK(static_cast<int>(p.entries().size()) == withAreas - areas);
    CHECK(std::none_of(p.entries().begin(), p.entries().end(), [](const Entry &e) { return e.empty; }));
    p.reload();
    CHECK(static_cast<int>(p.entries().size()) == withAreas - areas);
    p.setShowUnused(true);
    CHECK(static_cast<int>(p.entries().size()) == withAreas);

    /* by name the areas without a name go last, together */
    p.setSort(SortOrder::name, false);
    CHECK_FALSE(p.entries().front().empty);
}
