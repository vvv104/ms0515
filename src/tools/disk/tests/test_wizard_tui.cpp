/*
 * test_wizard_tui.cpp - the native disk wizard's screen, driven by events and
 * read back from a rendered screen: the marks, a toggle, a radio pick, the
 * files F2 saves, F3 opens and F5 builds.
 */

#include <doctest/doctest.h>

#include "../WizardTui.hpp"

#include <ms0515/disk/Build.hpp>

#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

using namespace ms0515;
using namespace ms0515::disk;
namespace fs = std::filesystem;

namespace {

const char kManifest[] = R"toml(
format  = 1
version = "test-1"

[system.omega]
title    = "OMEGA"
image    = "systems/omega.dsk"
media    = ["dz", "dv"]
requires = ["dz"]

[bundle.dz]
title = "DZ.SYS - floppy"
group = "System"
files = ["h/DZ.SYS"]

[bundle.macro-a]
title    = "MACRO build A"
group    = "Development"
provides = ["macro11"]
files    = ["d/a/MACRO.SAV"]

[bundle.macro-b]
title    = "MACRO build B"
group    = "Development"
provides = ["macro11"]
files    = ["d/b/MACRO.SAV"]

[bundle.pascal]
title    = "Pascal"
group    = "Development"
requires = ["macro11"]
prefer   = ["macro-a"]
files    = ["d/PAS1.SAV"]
)toml";

/* A bootable little exemplar: SWAP, a monitor, and the DZ.SYS the
 * repository serves loose. */
std::vector<uint8_t> handler()
{
    std::vector<uint8_t> h(3 * kBlock, 0);
    h[062] = 0x00; h[063] = 0x02; h[064] = 0x00; h[065] = 0x02; h[066] = 0x60;
    return h;
}

std::vector<uint8_t> exemplar()
{
    auto img = blankImage(true);
    initVolume(img, 0, true, {}, Vol::dv);
    std::vector<uint8_t> mon(6 * kBlock, 1);
    std::fill(mon.begin() + 4 * kBlock, mon.begin() + 5 * kBlock, uint8_t{0});
    putFile(img, 0, true, "SWAP.SYS", std::vector<uint8_t>(kBlock, 2), {}, Vol::dv);
    putFile(img, 0, true, "RT11SJ.SYS", mon, {}, Vol::dv);
    putFile(img, 0, true, "DZ.SYS", handler(), {}, Vol::dv);
    putFile(img, 0, true, "DV.SYS", handler(), {}, Vol::dv);
    writeBoot(img, 0, true, "RT11SJ", Vol::dv);
    return img;
}

Repository repository()
{
    auto files = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
    (*files)["systems/omega.dsk"] = exemplar();
    (*files)["h/DZ.SYS"] = handler();
    (*files)["d/a/MACRO.SAV"] = std::vector<uint8_t>(3 * kBlock, 3);
    (*files)["d/b/MACRO.SAV"] = std::vector<uint8_t>(4 * kBlock, 4);
    (*files)["d/PAS1.SAV"] = std::vector<uint8_t>(5 * kBlock, 5);
    Repository repo;
    for (const auto &kv : *files) repo.paths.push_back(kv.first);
    repo.read = [files](const std::string &p) -> std::optional<std::vector<uint8_t>> {
        const auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
    return repo;
}

std::string shown(tools::WizardTui &tui)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(110), ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, tui.render(110, 30));
    return screen.ToString();
}

void press(tools::WizardTui &tui, const ftxui::Event &e) { CHECK(tui.onEvent(e)); }

void type(tools::WizardTui &tui, const std::string &text)
{
    for (const char c : text) press(tui, ftxui::Event::Character(c));
}

void downTo(tools::WizardTui &tui, const std::string &title)
{
    press(tui, ftxui::Event::Character("/"));
    type(tui, title);
    press(tui, ftxui::Event::Return);
}

/* Find a row and press Space on it. */
void choose(tools::WizardTui &tui, const std::string &title)
{
    downTo(tui, title);
    press(tui, ftxui::Event::Character(" "));
}

/* The first two steps: a two-sided diskette, OMEGA on it. */
void ready(tools::WizardTui &tui)
{
    choose(tui, "dz - two sides");
    choose(tui, "OMEGA");
    REQUIRE(tui.model().ready());
}

fs::path scratch()
{
    const fs::path dir = fs::temp_directory_path() / "ms0515-wizard-tui";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}  /* namespace */

TEST_CASE("the wizard's screen: the diskette first, then the system, then the groups, folded") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    tools::WizardTui tui(m, repo, scratch());
    std::string s = shown(tui);
    CHECK(s.find("\xE2\x96\xBE Diskette") != std::string::npos);
    CHECK(s.find("( ) ss - one side, 400 KB") != std::string::npos);
    CHECK(s.find("\xE2\x96\xB8 Operating system") != std::string::npos);
    CHECK(s.find("choose the diskette first") != std::string::npos);
    CHECK(s.find("Space") != std::string::npos);                  /* the keys are always said */
    press(tui, ftxui::Event::F5);
    CHECK(tui.status() == "choose the diskette and the system first");

    press(tui, ftxui::Event::ArrowDown);                      /* from the heading down to dz */
    press(tui, ftxui::Event::ArrowDown);
    press(tui, ftxui::Event::Character(" "));
    s = shown(tui);
    CHECK(s.find("\xE2\x96\xBE Diskette") != std::string::npos);      /* chosen: still open, and saying what */
    CHECK(s.find("(\xE2\x80\xA2) dz - two sides, 800 KB") != std::string::npos);
    CHECK(s.find("\xE2\x96\xBE Operating system") != std::string::npos);
    CHECK(s.find("( ) OMEGA") != std::string::npos);
    CHECK(s.find("choose the system first") != std::string::npos);
    CHECK(s.find("DZ2: volume id") != std::string::npos);        /* a label for each side */

    type(tui, "GAMES");                                       /* the cursor went on to the label */
    press(tui, ftxui::Event::Return);                         /* kept: on to the next field */
    CHECK(tui.model().selection().volumeId == "GAMES");
    for (int field = 0; field < 3; ++field)                    /* Enter on a field goes past it as it is */
        press(tui, ftxui::Event::Return);
    press(tui, ftxui::Event::Character(" "));                 /* past the label: the first system */
    REQUIRE(tui.model().ready());
    s = shown(tui);
    CHECK(s.find("(\xE2\x80\xA2) OMEGA") != std::string::npos);     /* still in sight */
    CHECK(s.find("\xE2\x96\xB8 Development") != std::string::npos);
    CHECK(s.find("[ ] Pascal") == std::string::npos);
    CHECK(s.find("DZ0:") != std::string::npos);
    CHECK(s.find("DZ2:") != std::string::npos);

    press(tui, ftxui::Event::Return);                         /* and on to the first group */
    CHECK(shown(tui).find("[#] DZ.SYS - floppy") != std::string::npos);
    downTo(tui, "Pascal");
    s = shown(tui);
    CHECK(s.find("one of: macro11") != std::string::npos);
    CHECK(s.find("( ) MACRO build A") != std::string::npos);
    CHECK(s.find("[ ] Pascal") != std::string::npos);
}

TEST_CASE("Space on Pascal brings the preferred MACRO; picking the other swaps them") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    tools::WizardTui tui(m, repo, scratch());
    ready(tui);
    choose(tui, "Pascal");
    std::string s = shown(tui);
    CHECK(s.find("1 chosen, 1 added") != std::string::npos);    /* the group says it */
    CHECK(s.find("[x] Pascal") != std::string::npos);
    CHECK(s.find("(\xE2\x80\xA2) MACRO build A") != std::string::npos);
    CHECK(s.find("for Pascal") != std::string::npos);

    downTo(tui, "MACRO build B");
    press(tui, ftxui::Event::Character(" "));
    s = shown(tui);
    CHECK(s.find("( ) MACRO build A") != std::string::npos);
    CHECK(s.find("(\xE2\x80\xA2) MACRO build B") != std::string::npos);

    press(tui, ftxui::Event::Character(" "));                 /* Pascal needs one: it stays */
    CHECK(tui.status().find("required by Pascal") != std::string::npos);
}

TEST_CASE("F2 saves the choice, F3 opens it again, F5 builds a disk that boots") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    const fs::path dir = scratch();
    tools::WizardTui tui(m, repo, dir);
    ready(tui);
    choose(tui, "Pascal");

    press(tui, ftxui::Event::F2);
    press(tui, ftxui::Event::Return);                         /* the default name */
    REQUIRE(fs::exists(dir / "omega-dz.toml"));
    const SavedSelection saved = [&] {
        std::ifstream f(dir / "omega-dz.toml");
        return parseSelection(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()));
    }();
    CHECK(saved.collection == "test-1");
    CHECK(saved.selection.bundles == std::vector<std::string>{"pascal"});

    tools::WizardTui other(m, repo, dir);
    press(other, ftxui::Event::F3);
    type(other, "omega-dz.toml");                             /* nothing chosen yet: no name to offer */
    press(other, ftxui::Event::Return);
    CHECK(other.model().selection().bundles == std::vector<std::string>{"pascal"});

    press(other, ftxui::Event::F5);
    press(other, ftxui::Event::Return);
    REQUIRE(fs::exists(dir / "omega-dz.dsk"));
    CHECK(fs::file_size(dir / "omega-dz.dsk") == 819200);
    std::ifstream f(dir / "omega-dz.dsk", std::ios::binary);
    const std::vector<uint8_t> image(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{});
    CHECK(bootedMonitor(image, 0, true) == "RT11SJ");

    press(other, ftxui::Event::F10);
    CHECK(other.quit());
}

TEST_CASE("the label and START.COM are fields in the list: typing edits, Enter keeps, Esc drops") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    tools::WizardTui tui(m, repo, scratch());
    choose(tui, "dz - two sides");                            /* the cursor goes on to DZ0's volume id */
    type(tui, "MYDISK");
    press(tui, ftxui::Event::Return);
    CHECK(tui.model().selection().volumeId == "MYDISK");
    CHECK(shown(tui).find("[MYDISK") != std::string::npos);
    type(tui, "X");                                           /* on the owner now: typing starts an edit */
    press(tui, ftxui::Event::Escape);
    CHECK_FALSE(tui.model().selection().owner.has_value());
    type(tui, "VVV");
    press(tui, ftxui::Event::Return);
    CHECK(tui.model().selection().owner == "VVV");

    choose(tui, "OMEGA");
    downTo(tui, "START.COM");
    press(tui, ftxui::Event::ArrowDown);                      /* the new line under the heading */
    type(tui, "R PAS1");
    press(tui, ftxui::Event::Return);
    CHECK(tui.model().selection().startup == std::vector<std::string>{"R PAS1"});
    const std::string s = shown(tui);
    CHECK(s.find("[R PAS1") != std::string::npos);
    CHECK(s.find("1 line") != std::string::npos);
    CHECK(s.find("7Startup") == std::string::npos);            /* no keys of their own any more */

    downTo(tui, "START.COM");
    press(tui, ftxui::Event::ArrowDown);
    press(tui, ftxui::Event::Delete);                         /* Del on a line takes it out */
    CHECK_FALSE(tui.model().selection().startup.has_value());

    choose(tui, "dv - one DV");                               /* another diskette: on to its label again */
    type(tui, "DVDISK");                                      /* typing on MYDISK starts afresh */
    press(tui, ftxui::Event::Return);
    CHECK(tui.model().selection().volumeId == "DVDISK");
    press(tui, ftxui::Event::ArrowUp);
    type(tui, "X");
    press(tui, ftxui::Event::Delete);                         /* Del in an edit empties the box */
    type(tui, "NEW");
    press(tui, ftxui::Event::Return);
    CHECK(tui.model().selection().volumeId == "NEW");
    const std::string labels = shown(tui);
    CHECK(labels.find("[NEW         ]") != std::string::npos);  /* every label box twelve wide */
    CHECK(labels.find("[VVV         ]") != std::string::npos);
}

TEST_CASE("Enter walks the label field by field, on to the systems - whichever diskette, however often") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    tools::WizardTui tui(m, repo, scratch());
    choose(tui, "dz - two sides");
    CHECK(tui.cursorKey() == kVolumeIdField);
    for (const char *key : {kOwnerField, kSecondVolumeIdField, kSecondOwnerField, "omega"}) {
        press(tui, ftxui::Event::Return);
        CHECK(tui.cursorKey() == key);
    }
    press(tui, ftxui::Event::Character(" "));
    REQUIRE(tui.model().ready());
    choose(tui, "dv - one DV");                               /* two fields now, a system chosen */
    CHECK(tui.cursorKey() == kVolumeIdField);
    press(tui, ftxui::Event::Return);
    CHECK(tui.cursorKey() == kOwnerField);
    press(tui, ftxui::Event::Return);
    CHECK(tui.cursorKey() == "omega");
    choose(tui, "dz - two sides");
    for (int field = 0; field < 4; ++field) press(tui, ftxui::Event::Return);
    CHECK(tui.cursorKey() == "omega");
    CHECK_FALSE(tui.model().selection().volumeId.has_value());  /* walked past, nothing typed */
}

TEST_CASE("a system that does not go on the diskette is greyed; another diskette later drops it") {
    const Manifest m = parseManifest(kManifest);
    const Repository repo = repository();
    tools::WizardTui tui(m, repo, scratch());
    choose(tui, "ss - one side");
    CHECK(shown(tui).find("only on dz, dv") != std::string::npos);
    choose(tui, "OMEGA");
    CHECK(tui.status() == "OMEGA goes only on dz, dv");
    CHECK_FALSE(tui.model().ready());

    choose(tui, "dv - one DV");
    choose(tui, "OMEGA");
    CHECK(tui.model().media() == Media::dv);
    CHECK(shown(tui).find("DV0:") != std::string::npos);

    choose(tui, "ss - one side");
    CHECK(tui.model().system().empty());
    CHECK(tui.status().find("OMEGA dropped") != std::string::npos);
    CHECK(shown(tui).find("choose the system first") != std::string::npos);
}
