/*
 * test_file_dialog.cpp - the wizard's file window: what it lists, picking
 * with the cursor, going into a directory, a name typed, a file to open
 * that must be there and one to write that is asked about.
 */

#include <doctest/doctest.h>

#include "../FileDialog.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ms0515;
namespace fs = std::filesystem;

namespace {

fs::path tree()
{
    const fs::path dir = fs::temp_directory_path() / "ms0515-file-dialog";
    fs::remove_all(dir);
    fs::create_directories(dir / "choices");
    std::ofstream(dir / "games.toml") << "x";
    std::ofstream(dir / "devel.toml") << "x";
    std::ofstream(dir / "games.dsk") << "x";
    std::ofstream(dir / "choices" / "rosa.toml") << "x";
    return dir;
}

using Result = tools::FileDialog::Result;

Result press(tools::FileDialog &d, const ftxui::Event &e) { return d.onEvent(e); }

}  /* namespace */

TEST_CASE("the file window lists the directories and the files of its extension, and picks with the cursor") {
    const fs::path dir = tree();
    tools::FileDialog d("Open a choice", tools::FileDialog::Mode::open, dir, ".toml");
    CHECK(d.entries() == std::vector<std::string>{"../", "choices/", "devel.toml", "games.toml"});
    CHECK(d.selected() == -1);
    press(d, ftxui::Event::ArrowDown);
    press(d, ftxui::Event::ArrowDown);
    press(d, ftxui::Event::ArrowDown);
    CHECK(d.name() == "devel.toml");                          /* a file picked: its name taken */
    CHECK(press(d, ftxui::Event::Return) == Result::accepted);
    CHECK(d.path() == dir / "devel.toml");
}

TEST_CASE("Enter on a directory goes into it; .. comes back") {
    const fs::path dir = tree();
    tools::FileDialog d("Open a choice", tools::FileDialog::Mode::open, dir, ".toml");
    press(d, ftxui::Event::ArrowDown);
    press(d, ftxui::Event::ArrowDown);
    CHECK(press(d, ftxui::Event::Return) == Result::none);
    CHECK(d.dir() == (dir / "choices").lexically_normal());
    CHECK(d.entries() == std::vector<std::string>{"../", "rosa.toml"});
    press(d, ftxui::Event::ArrowDown);
    CHECK(press(d, ftxui::Event::Return) == Result::none);
    CHECK(fs::equivalent(d.dir(), dir));
}

TEST_CASE("a name typed: the extension added; to open it must be there") {
    const fs::path dir = tree();
    tools::FileDialog d("Open a choice", tools::FileDialog::Mode::open, dir, ".toml");
    for (const char c : std::string("nothing")) press(d, ftxui::Event::Character(c));
    CHECK(press(d, ftxui::Event::Return) == Result::none);
    CHECK_FALSE(d.problem().empty());
    press(d, ftxui::Event::Delete);                           /* Del empties the name */
    CHECK(d.name().empty());
    for (const char c : std::string("games")) press(d, ftxui::Event::Character(c));
    CHECK(press(d, ftxui::Event::Return) == Result::accepted);
    CHECK(d.path() == dir / "games.toml");
    tools::FileDialog gone("Open a choice", tools::FileDialog::Mode::open, dir, ".toml");
    CHECK(press(gone, ftxui::Event::Escape) == Result::cancelled);
}

TEST_CASE("to write over a file that is there, the window asks first") {
    const fs::path dir = tree();
    tools::FileDialog d("Build the disk", tools::FileDialog::Mode::write, dir, ".dsk", "games.dsk");
    CHECK(press(d, ftxui::Event::Return) == Result::none);
    CHECK(d.asking());
    CHECK(press(d, ftxui::Event::Escape) == Result::none);    /* no: back to the name */
    CHECK_FALSE(d.asking());
    CHECK(press(d, ftxui::Event::Return) == Result::none);
    CHECK(press(d, ftxui::Event::Return) == Result::accepted);
    tools::FileDialog fresh("Build the disk", tools::FileDialog::Mode::write, dir, ".dsk", "new");
    CHECK(press(fresh, ftxui::Event::Return) == Result::accepted);
    CHECK(fresh.path() == dir / "new.dsk");
}
