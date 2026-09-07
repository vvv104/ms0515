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
#include <vector>

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
    std::string last;
    for (const auto &e : vol->list()) if (!e.empty) last = e.name;   /* the last file */
    const auto shortBefore = shot(c, 80, 12);
    CHECK(rowText(shortBefore, 1).find("DZ0: osa.dsk") != std::string::npos);
    CHECK(rowText(shortBefore, 9).find("files") != std::string::npos);
    CHECK_FALSE(anyRowHas(shortBefore, last));
    CHECK(c.onEvent(ftxui::Event::End));
    CHECK(c.onEvent(ftxui::Event::ArrowUp));       /* End lands on the free space after the last file */
    const auto shortAfter = shot(c, 80, 12);
    CHECK(anyRowHas(shortAfter, last));
    std::string first;
    for (const auto &e : vol->list()) if (!e.empty) { first = e.name; break; }   /* the first file scrolled away */
    CHECK_FALSE(anyRowHas(shortAfter, first));
    /* the current-file line above the bottom border names it too */
    CHECK(rowText(shortAfter, 8).find(last) != std::string::npos);

    /* the title sits one cell in from the corner, with a plain cell before it */
    const std::string titleRow = rowText(shortAfter, 1);
    CHECK(titleRow.find(" DZ0: osa.dsk ") != std::string::npos);
    CHECK(shortAfter.PixelAt(1, 1).character == "\xE2\x94\x80");   /* the border line runs up to the title */
    CHECK(shortAfter.PixelAt(1, 1).background_color != ftxui::Color::Cyan);
    CHECK(shortAfter.PixelAt(2, 1).background_color == ftxui::Color::Cyan);

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
    CHECK(Commander::guestRowsTop(20) == 16);
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

    /* a menu item runs its action: Left -> Sort order... opens the dialog */
    CHECK(d.c.onEvent(ftxui::Event::F9));
    CHECK(d.c.onEvent(ftxui::Event::ArrowDown));
    CHECK(d.c.onEvent(ftxui::Event::ArrowDown));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    auto sort = shot(d.c, 80, 25);
    CHECK(anyRowHas(sort, "(*) Offset"));
    CHECK(anyRowHas(sort, "[ ] Reverse"));
    CHECK(d.c.onEvent(ftxui::Event::Escape));
    CHECK_FALSE(d.c.modal());
}

TEST_CASE("the columns are Name, blocks with the P flag, and the date the mc way; the cursor bar runs through the rules")
{
    TwoDisks d;
    auto screen = shot(d.c, 80, 25);
    CHECK(anyRowHas(screen, "Blk P"));
    CHECK(anyRowHas(screen, "Date"));
    /* DIR.SAV: 20 blocks, protected, 1990-12-27 */
    bool found = false;
    for (int y = 0; y < screen.dimy() && !found; ++y) {
        const std::string row = rowText(screen, y);
        if (row.find("DIR.SAV") == std::string::npos || row.find("Dec 27  1990") == std::string::npos) continue;
        CHECK(row.find("20 P") != std::string::npos);          /* the flag stands apart, under the P of "Blk P" */
        CHECK(row.find("20 P") < row.find("Dec 27"));
        found = true;
    }
    CHECK(found);
    /* the cursor row: the rule cells carry the bar's colour too */
    const int cursorRow = 3;   /* menu bar, the border, the header, then the first file */
    const std::string row = rowText(screen, cursorRow);
    REQUIRE(row.find("\xE2\x94\x82") != std::string::npos);
    for (int x = 1; x < 38; ++x)   /* inside the left panel, its own border at 39 aside */
        if (screen.PixelAt(x, cursorRow).character == "\xE2\x94\x82") CHECK(screen.PixelAt(x, cursorRow).background_color == ftxui::Color::Cyan);
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
    int files = 0;
    for (const auto &e : vol->list()) if (!e.empty) ++files;
    const int rest = files - savs;
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
    CHECK(rowText(shot(d.c, 80, 25), 21).find("Dec 27  1990") != std::string::npos);
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

TEST_CASE("the Offset column, the unused areas as '< UNUSED >' or the deleted file's name, and the Options toggle that hides them")
{
    TwoDisks d;
    CHECK(anyRowHas(shot(d.c, 80, 25), "Offset"));
    CHECK(d.c.onEvent(ftxui::Event::End));         /* the free space is the volume's tail */
    CHECK(anyRowHas(shot(d.c, 80, 25), "< UNUSED >"));
    CHECK(d.c.onEvent(ftxui::Event::Home));
    /* F8 on PIP.SAV, then its area carries the name */
    const auto left = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    const auto names = left->list();
    for (size_t i = 0; i < names.size() && names[i].name != "PIP.SAV"; ++i) d.c.onEvent(ftxui::Event::ArrowDown);
    CHECK(d.c.onEvent(ftxui::Event::F8));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    auto gone = shot(d.c, 80, 25);
    CHECK(anyRowHas(gone, "PIP.SAV"));                 /* the area, under the old name */
    CHECK(rowText(gone, 21).find("PIP.SAV") != std::string::npos);   /* the cursor stayed on it */
    /* the user menu brings it back: Undelete... with the name prefilled */
    CHECK(d.c.onEvent(ftxui::Event::F2));
    CHECK(d.c.onEvent(ftxui::Event::End));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    auto undel = shot(d.c, 80, 25);
    CHECK(anyRowHas(undel, "Undelete"));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    CHECK_FALSE(d.c.modal());
    auto fresh = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    CHECK(fresh->find("PIP.SAV").has_value());

    /* Options -> Show unused areas: off */
    CHECK(d.c.onEvent(ftxui::Event::F9));
    for (int i = 0; i < 3; ++i) CHECK(d.c.onEvent(ftxui::Event::ArrowRight));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    CHECK(anyRowHas(shot(d.c, 80, 25), "Show unused areas"));
    CHECK(d.c.onEvent(ftxui::Event::End));
    CHECK(d.c.onEvent(ftxui::Event::Return));
    CHECK_FALSE(d.c.modal());
    CHECK(d.c.onEvent(ftxui::Event::End));
    CHECK_FALSE(anyRowHas(shot(d.c, 80, 25), "< UNUSED >"));
}

TEST_CASE("a dialog and a menu hide what lies under them")
{
    TwoDisks d;
    CHECK(d.c.onEvent(ftxui::Event::F4));
    auto dlg = shot(d.c, 80, 25);
    int titleRow = -1;
    for (int y = 0; y < dlg.dimy(); ++y) if (rowText(dlg, y).find("Left panel") != std::string::npos) titleRow = y;
    REQUIRE(titleRow >= 0);
    /* the rows of the box carry no file name of the panels beneath */
    for (int y = titleRow + 1; y < titleRow + 4; ++y) {
        const std::string row = rowText(dlg, y);
        const auto open = row.find("\xE2\x94\x82"), close = row.rfind("\xE2\x94\x82");
        REQUIRE(open != std::string::npos);
        const std::string inside = row.substr(open, close - open);
        CHECK(inside.find("SWAP.SYS") == std::string::npos);
        CHECK(inside.find("Dec 27") == std::string::npos);
    }
    /* and the frame is not welded to the panel borders it lies over: the
     * junction characters FTXUI makes of touching borders must not appear
     * on the dialog's title row once the screen is printed */
    const std::string printed = dlg.ToString();
    std::vector<std::string> printedRows;
    for (size_t at = 0; at < printed.size();) {
        size_t nl = printed.find('\n', at);
        if (nl == std::string::npos) nl = printed.size();
        printedRows.push_back(printed.substr(at, nl - at));
        at = nl + 1;
    }
    REQUIRE(static_cast<int>(printedRows.size()) > titleRow);
    const std::string &frameRow = printedRows[static_cast<size_t>(titleRow)];
    CHECK(frameRow.find("Left panel") != std::string::npos);
    for (const char *junction : {"\xE2\x94\xA4", "\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xAC", "\xE2\x94\xB4"})
        CHECK(frameRow.find(junction) == std::string::npos);
    CHECK(d.c.onEvent(ftxui::Event::Escape));
}

TEST_CASE("Enter runs the program under the cursor: its bare name on the default device, RUN with the device anywhere else; anything else it ignores")
{
    Scratch s("run");
    const auto osa = s.disk("test_osa.dsk", "osa.dsk");
    const auto rod = s.disk("test_rod.dsk", "rod.dsk");
    Mounts m;
    REQUIRE(m.mount(Slot::driveA, osa, 0).empty());
    REQUIRE(m.mount(Slot::driveB, rod, 0).empty());
    ms0515::app::Config config;
    std::string typed;
    CommanderHooks hooks;
    hooks.runInGuest = [&typed](const std::string &line) { typed = line; };
    Commander c(std::move(m), config, std::move(hooks));
    const auto vol = Location::open(Device{"DZ0:", osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    const auto names = vol->list();
    for (size_t i = 0; i < names.size() && names[i].name != "DIR.SAV"; ++i) c.onEvent(ftxui::Event::ArrowDown);
    CHECK(c.onEvent(ftxui::Event::Return));
    CHECK(typed == "DIR");            /* the system device: the bare name runs it */
    CHECK_FALSE(c.modal());
    /* a .COM is an indirect command file */
    c.onEvent(ftxui::Event::Home);
    for (size_t i = 0; i < names.size() && names[i].name != "START.COM"; ++i) c.onEvent(ftxui::Event::ArrowDown);
    if (vol->find("START.COM")) {
        CHECK(c.onEvent(ftxui::Event::Return));
        CHECK(typed == "@START");
    }

    /* another device: the monitor takes no bare name there - it answers
     * "DZ0:PIP" with "invalid command" - so the command says RUN */
    typed.clear();
    CHECK(c.onEvent(ftxui::Event::Tab));
    const auto right = Location::open(Device{"DZ1:", rod, disk::VolumeSpec{disk::Vol::floppy, 0}});
    REQUIRE(right.has_value());
    std::string program;
    for (const auto &e : right->list()) if (Location::isProgram(e.name) && e.name.ends_with(".SAV")) { program = e.name; break; }
    REQUIRE_FALSE(program.empty());
    for (const auto &e : right->list()) { if (e.name == program) break; c.onEvent(ftxui::Event::ArrowDown); }
    CHECK(c.onEvent(ftxui::Event::Return));
    CHECK(typed == "RUN DZ1:" + program.substr(0, program.rfind('.')));
    CHECK(c.onEvent(ftxui::Event::Tab));
    /* a file the machine cannot run: Enter does nothing at all - no viewer,
     * and not a word in the status line, as other commanders ignore it */
    typed.clear();
    c.onEvent(ftxui::Event::Home);
    for (size_t i = 0; i < names.size() && names[i].name != "SWAP.SYS"; ++i) c.onEvent(ftxui::Event::ArrowDown);
    const std::string statusBefore = rowText(shot(c, 80, 25), 23);
    CHECK(c.onEvent(ftxui::Event::Return));
    CHECK(typed.empty());
    CHECK_FALSE(c.modal());
    CHECK(rowText(shot(c, 80, 25), 23) == statusBefore);
    CHECK(c.onEvent(ftxui::Event::F3));
    CHECK(c.modal());
    CHECK(c.onEvent(ftxui::Event::Escape));

    /* without a runner - the standalone program - Enter does nothing either */
    Mounts m2;
    REQUIRE(m2.mount(Slot::driveA, osa, 0).empty());
    Commander plain(std::move(m2), config);
    for (size_t i = 0; i < names.size() && names[i].name != "DIR.SAV"; ++i) plain.onEvent(ftxui::Event::ArrowDown);
    CHECK(plain.onEvent(ftxui::Event::Return));
    CHECK_FALSE(plain.modal());
}

TEST_CASE("a program is green in the panel, as mc paints executables; a marked one is yellow all the same")
{
    TwoDisks d;
    const auto screen = shot(d.c, 80, 25);
    const auto rowOf = [&screen](const std::string &name) {
        for (int y = 0; y < screen.dimy(); ++y)
            if (rowText(screen, y).find(name) != std::string::npos) return y;
        return -1;
    };
    const int program = rowOf("DIR.SAV");
    const int data = rowOf("SWAP.SYS");
    REQUIRE(program > 0);
    REQUIRE(data > 0);
    CHECK(screen.PixelAt(2, program).foreground_color == ftxui::Color::GreenLight);
    CHECK(screen.PixelAt(2, data).foreground_color != ftxui::Color::GreenLight);
    /* the colour is the text's alone: the rules between the columns keep
     * the panel's own, as they do in mc */
    const std::string row = rowText(screen, program);
    const size_t rule = row.find("\xE2\x94\x82");
    REQUIRE(rule != std::string::npos);
    int ruleColumn = 0;
    for (size_t at = 0; at < rule; ++ruleColumn) at += static_cast<size_t>((row[at] & 0xC0) == 0xC0 ? 3 : 1);
    CHECK(screen.PixelAt(ruleColumn, program).character == "\xE2\x94\x82");
    CHECK(screen.PixelAt(ruleColumn, program).foreground_color == ftxui::Color::White);
    /* Insert on the program: marked is yellow, over the green */
    const auto vol = Location::open(Device{"DZ0:", d.osa, disk::VolumeSpec{disk::Vol::floppy, 0}});
    const auto names = vol->list();
    for (size_t i = 0; i < names.size() && names[i].name != "DIR.SAV"; ++i) d.c.onEvent(ftxui::Event::ArrowDown);
    CHECK(d.c.onEvent(ftxui::Event::Insert));
    const auto marked = shot(d.c, 80, 25);
    CHECK(rowText(marked, program).find("DIR.SAV") != std::string::npos);   /* the cursor has stepped past it */
    CHECK(marked.PixelAt(2, program).foreground_color == ftxui::Color::Yellow);
}
