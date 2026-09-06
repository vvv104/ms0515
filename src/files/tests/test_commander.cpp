/*
 * test_commander.cpp — the panels drawn into a screen of a given size:
 * the title on the top border, the summary on the bottom one, and the
 * cursor kept in view when the terminal is short.
 */
#include "Commander.hpp"
#include "Location.hpp"
#include "scratch.hpp"

#include "ms0515/app/Config.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace ms0515::files;
namespace disk = ms0515::disk;

namespace {

std::string rowText(const ftxui::Screen &screen, int y)
{
    std::string s;
    for (int x = 0; x < screen.dimx(); ++x) s += screen.PixelAt(x, y).character;
    return s;
}

ftxui::Screen shot(Commander &c, int width, int height)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, c.render(width, height));
    return screen;
}

std::string fmt_files(int n)
{
    return n == 1 ? "in 1 file" : "in " + std::to_string(n) + " files";
}

bool anyRowHas(const ftxui::Screen &screen, const std::string &needle)
{
    for (int y = 0; y < screen.dimy(); ++y)
        if (rowText(screen, y).find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST_CASE("the title sits on the top border, the summary on the bottom one; a short terminal still shows the cursor")
{
    Scratch s("commander");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    Mounts m;
    REQUIRE(m.mount(Slot::driveA, osa, 0).empty());
    ms0515::app::Config config;
    Commander c(std::move(m), config);

    const auto tall = shot(c, 80, 25);
    CHECK(rowText(tall, 0).find("Left") != std::string::npos);            /* the menu bar */
    CHECK(rowText(tall, 1).find("DZ0: osa.dsk") != std::string::npos);
    CHECK(rowText(tall, 24).find("Quit") != std::string::npos);            /* the key bar */
    /* the bottom border of the panels carries the summary, just above the status line and the key bar */
    CHECK(rowText(tall, 22).find("files") != std::string::npos);
    CHECK(anyRowHas(tall, "DIR.SAV"));

    /* twelve rows: the list holds five; End must bring the last file into view */
    const auto vol = Location::open(Device{"DZ0:", osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    REQUIRE(vol.has_value());
    const std::string last = vol->list().back().name;
    const auto shortBefore = shot(c, 80, 12);
    CHECK(rowText(shortBefore, 1).find("DZ0: osa.dsk") != std::string::npos);
    CHECK(rowText(shortBefore, 9).find("files") != std::string::npos);
    CHECK_FALSE(anyRowHas(shortBefore, last));
    CHECK(c.onEvent(ftxui::Event::End));
    const auto shortAfter = shot(c, 80, 12);
    CHECK(anyRowHas(shortAfter, last));
    CHECK_FALSE(anyRowHas(shortAfter, "DIR.SAV"));
    /* the current-file line above the bottom border names it too */
    CHECK(rowText(shortAfter, 8).find(last) != std::string::npos);

    /* the cursor row is painted cyan, the column header yellow - the
     * summary on the bottom border must not repaint them */
    bool cyanRow = false, yellowHeader = false;
    for (int y = 0; y < shortAfter.dimy(); ++y) {
        if (rowText(shortAfter, y).find(last) != std::string::npos && shortAfter.PixelAt(2, y).background_color == ftxui::Color::Cyan) cyanRow = true;
        if (rowText(shortAfter, y).find("Name") != std::string::npos && shortAfter.PixelAt(2, y).foreground_color == ftxui::Color::Yellow) yellowHeader = true;
    }
    CHECK(cyanRow);
    CHECK(yellowHeader);
}

TEST_CASE("with the host's rows under the panels the page still fits the height")
{
    Scratch s("commander-guest");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    Mounts m;
    REQUIRE(m.mount(Slot::driveA, osa, 0).empty());
    ms0515::app::Config config;
    Commander c(std::move(m), config);
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, c.render(80, 20, ftxui::vbox({ftxui::text(".DIR DZ0:"), ftxui::text(".")})));
    CHECK(rowText(screen, 19).find("Quit") != std::string::npos);
    CHECK(rowText(screen, 16).find(".DIR DZ0:") != std::string::npos);   /* the two guest rows: 16, 17 */
    CHECK(rowText(screen, 15).find("files") != std::string::npos);      /* the bottom border above them */
    CHECK(rowText(screen, 1).find("DZ0: osa.dsk") != std::string::npos);
}

namespace {

/* Both drives: osa.dsk on DZ0:, rod.dsk on DZ1: / DZ3:. */
struct TwoDisks {
    Scratch s{"two"};
    fs::path osa = s.disk("test_osa.dsk", "osa.dsk");
    fs::path rod = s.disk("test_rod.dsk", "rod.dsk");
    ms0515::app::Config config;
    Commander c;
    TwoDisks() : c(mounted(), config) {}
    Mounts mounted()
    {
        Mounts m;
        REQUIRE(m.mount(Slot::driveA, osa, 0).empty());
        REQUIRE(m.mount(Slot::driveB, rod, 0).empty());
        return m;
    }
    void type(const std::string &keys) { for (const char ch : keys) c.onEvent(ftxui::Event::Character(std::string(1, ch))); }
};

} // namespace

TEST_CASE("F9 pulls the menu down; Esc puts it away")
{
    TwoDisks d;
    CHECK(d.c.onEvent(ftxui::Event::F9));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    auto open = shot(d.c, 80, 25);
    CHECK(anyRowHas(open, "Sort order"));
    CHECK(d.c.modal());
    CHECK(d.c.onEvent(ftxui::Event::Escape));
    CHECK(d.c.onEvent(ftxui::Event::Escape));
    CHECK_FALSE(d.c.modal());
    CHECK_FALSE(anyRowHas(shot(d.c, 80, 25), "Sort order"));
}

TEST_CASE("+ marks by pattern through the Select dialog, * inverts, and the panel says how many")
{
    TwoDisks d;
    const auto vol = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    int savs = 0;
    for (const auto &e : vol->list()) if (e.name.ends_with(".SAV")) ++savs;
    CHECK(d.c.onEvent(ftxui::Event::Character("+")));
    CHECK(anyRowHas(shot(d.c, 80, 25), "Select"));
    d.type("*.SAV");
    CHECK(d.c.onEvent(ftxui::Event::Return));
    CHECK(anyRowHas(shot(d.c, 80, 25), fmt_files(savs)));
    CHECK(d.c.onEvent(ftxui::Event::Character("*")));
    const int rest = static_cast<int>(vol->list().size()) - savs;
    CHECK(anyRowHas(shot(d.c, 80, 25), fmt_files(rest)));
}

TEST_CASE("Ctrl+U swaps the panels")
{
    TwoDisks d;
    auto before = shot(d.c, 80, 25);
    const std::string row1 = rowText(before, 1);
    CHECK(row1.find("DZ0: osa.dsk") < row1.find("DZ1: rod.dsk"));
    CHECK(d.c.onEvent(ftxui::Event::Special(std::string(""))));
    const std::string after = rowText(shot(d.c, 80, 25), 1);
    CHECK(after.find("DZ1: rod.dsk") < after.find("DZ0: osa.dsk"));
}

TEST_CASE("F5 copies to the device in the 'to:' line; a file that exists asks, mc's way")
{
    TwoDisks d;
    /* the cursor on DIR.SAV, which rod's side 0 has too */
    const auto left = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    const auto names = left->list();
    for (size_t i = 0; i < names.size() && names[i].name != "DIR.SAV"; ++i) d.c.onEvent(ftxui::Event::ArrowDown);
    CHECK(rowText(shot(d.c, 80, 25), 21).find("DIR.SAV") != std::string::npos);   /* the current-file line */
    CHECK(d.c.onEvent(ftxui::Event::F5));
    auto dlg = shot(d.c, 80, 25);
    CHECK(anyRowHas(dlg, "Copy file \"DIR.SAV\""));
    CHECK(anyRowHas(dlg, "DZ1:"));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    auto exists = shot(d.c, 80, 25);
    CHECK(anyRowHas(exists, "File exists"));
    CHECK(anyRowHas(exists, "Overwrite this file?"));
    CHECK(d.c.onEvent(ftxui::Event::Character("y")));
    CHECK_FALSE(d.c.modal());
    const auto right = Location::open(Device{"DZ1:", d.rod, disk::VolumeSpec{disk::Vol::floppy, 0}});
    CHECK(*right->read("DIR.SAV") == *left->read("DIR.SAV"));
}

TEST_CASE("F8 asks before deleting, F6 with a bare name renames")
{
    TwoDisks d;
    auto left = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    const auto names = left->list();
    for (size_t i = 0; i < names.size() && names[i].name != "PIP.SAV"; ++i) d.c.onEvent(ftxui::Event::ArrowDown);
    CHECK(d.c.onEvent(ftxui::Event::F6));
    auto move = shot(d.c, 80, 25);
    CHECK(anyRowHas(move, "Move file \"PIP.SAV\""));
    for (int i = 0; i < 8; ++i) d.c.onEvent(ftxui::Event::Backspace);   /* the prefilled device name away */
    d.type("PIPX.SAV");
    CHECK(d.c.onEvent(ftxui::Event::Return));
    CHECK_FALSE(d.c.modal());
    CHECK(left->reload());
    CHECK(left->find("PIPX.SAV").has_value());
    CHECK_FALSE(left->find("PIP.SAV").has_value());

    CHECK(d.c.onEvent(ftxui::Event::F8));
    auto del = shot(d.c, 80, 25);
    CHECK(anyRowHas(del, "Delete file \"PIPX.SAV\"?"));
    CHECK(d.c.onEvent(ftxui::Event::Return));      /* Yes is the default */
    CHECK_FALSE(d.c.modal());
    CHECK(left->reload());
    CHECK_FALSE(left->find("PIPX.SAV").has_value());
}
