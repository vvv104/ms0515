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
    CHECK(rowText(tall, 0).find("DZ0: osa.dsk") != std::string::npos);
    CHECK(rowText(tall, 24).find("Quit") != std::string::npos);            /* the key bar */
    /* the bottom border of the panels carries the summary, just above the status line and the key bar */
    CHECK(rowText(tall, 22).find("files") != std::string::npos);
    CHECK(anyRowHas(tall, "DIR.SAV"));

    /* twelve rows: the list holds five; End must bring the last file into view */
    const auto vol = Location::open(Device{"DZ0:", osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    REQUIRE(vol.has_value());
    const std::string last = vol->list().back().name;
    const auto shortBefore = shot(c, 80, 12);
    CHECK(rowText(shortBefore, 0).find("DZ0: osa.dsk") != std::string::npos);
    CHECK(rowText(shortBefore, 9).find("files") != std::string::npos);
    CHECK_FALSE(anyRowHas(shortBefore, last));
    CHECK(c.onEvent(ftxui::Event::End));
    const auto shortAfter = shot(c, 80, 12);
    CHECK(anyRowHas(shortAfter, last));
    CHECK_FALSE(anyRowHas(shortAfter, "DIR.SAV"));
    /* the current-file line above the bottom border names it too */
    CHECK(rowText(shortAfter, 8).find(last) != std::string::npos);
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
    CHECK(rowText(screen, 0).find("DZ0: osa.dsk") != std::string::npos);
}
