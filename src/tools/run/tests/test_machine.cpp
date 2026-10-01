/*
 * test_machine.cpp - ms0515-run's machine, from what the binary carries to
 * a program run and over: the same calls main() makes, without a terminal
 * or a window.
 */

#include <doctest/doctest.h>

#include "ConsoleText.hpp"
#include "Embedded.hpp"
#include "Machine.hpp"
#include "Starter.hpp"
#include "ZeroRun.hpp"

#include <ms0515/Terminal.hpp>
#include <ms0515/disk/Image.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ms0515::run::Machine;

#ifndef TESTS_BUILD_DIR
#error "TESTS_BUILD_DIR must be defined by the build system"
#endif
#ifndef RT11_SYSTEM_DIR
#error "RT11_SYSTEM_DIR must be defined by the build system"
#endif

namespace {

fs::path freshDir(const char *name)
{
    fs::path d = fs::path(TESTS_BUILD_DIR) / "run-fixtures" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void writeFile(const fs::path &p, const std::vector<uint8_t> &bytes)
{
    std::ofstream(p, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

std::string readText(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::set<std::string> filesIn(const fs::path &dir)
{
    std::set<std::string> names;
    for (const auto &de : fs::directory_iterator(dir))
        names.insert(de.path().filename().string());
    return names;
}

/* A program of `code` at 1000, as LINK would save it. */
std::vector<uint8_t> program(const std::vector<uint16_t> &code,
                             const std::string &text = {})
{
    std::vector<uint8_t> image(2 * 512, 0);
    const auto put = [&](std::size_t at, uint16_t w) {
        image[at] = static_cast<uint8_t>(w & 0xFF);
        image[at + 1] = static_cast<uint8_t>(w >> 8);
    };
    std::size_t at = 01000;
    for (uint16_t w : code) { put(at, w); at += 2; }
    for (char c : text) image[at++] = static_cast<uint8_t>(c);
    put(040, 01000);
    put(042, 01000);
    put(050, static_cast<uint16_t>((at + 1) & ~1u));
    image[0360] = 0xC0;
    return image;
}

/* .PRINT the text that follows the code, then .EXIT. */
std::vector<uint8_t> helloProgram(const std::string &text)
{
    return program({
        0012700, 01010,     /* MOV #TEXT,R0 */
        0104351,            /* .PRINT       */
        0104350,            /* .EXIT        */
    }, text + '\0');
}

std::vector<std::string> screenRows(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    const auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        auto row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        if (row == "_") row.clear();                    /* the cursor alone */
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string screenText(const ms0515::Emulator &emu)
{
    std::string text;
    for (const auto &row : screenRows(emu))
        if (!row.empty()) text += row + "\n";
    return text;
}

/* Run to the program's end, then the few frames the last of its output
 * takes to reach the screen. */
/* `printed` receives what the program printed, as a file would get it. */
bool runToEnd(Machine &machine, std::string *printed = nullptr,
              int frames = 20000)
{
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string all;
    while (frames-- > 0 && machine.step())
        all += text.convert(machine.takeOutput());
    if (!machine.ended())
        return false;
    all += text.convert(machine.drainOutput());
    if (printed) *printed = all;
    return true;
}

}  /* namespace */

TEST_SUITE("ms0515-run machine") {

TEST_CASE("the carried diskette holds the system and the starter, nothing else") {
    auto image = ms0515::run::unpackZeroRuns(ms0515::run::embedded::disk);
    REQUIRE(image.has_value());
    const auto opened = ms0515::disk::openImage(*image);
    REQUIRE(opened.has_value());

    std::set<std::string> names;
    for (const auto &e : opened->directory.entries)
        if (e.isPermanent()) names.insert(e.name);
    CHECK(names == std::set<std::string>{"RT11SJ.SYS", "SWAP.SYS", "DZ.SYS",
                                         "TT.SYS", "HD.SYS", "START.SAV"});
}

TEST_CASE("the carried state is the starter waiting on a blank screen") {
    Machine machine;
    auto &emu = machine.emulator();
    CHECK(ms0515::run::starterWaiting(emu));
    CHECK_FALSE(ms0515::run::monitorInControl(emu));
    CHECK(screenText(emu).empty());
    CHECK(emu.isHires());
    CHECK(emu.hdEnabled());

    /* It stays so for as long as nobody hands it a command. */
    for (int i = 0; i < 200; ++i) (void)emu.stepFrame();
    CHECK(ms0515::run::starterWaiting(emu));
    CHECK(screenText(emu).empty());
}

TEST_CASE("a program runs from its folder, prints, and the run is over") {
    const auto dir = freshDir("hello");
    writeFile(dir / "hello.sav", helloProgram("HELLO FROM THE FOLDER"));

    Machine machine;
    REQUIRE(machine.start(dir / "hello.sav", {}));
    std::string printed;
    REQUIRE(runToEnd(machine, &printed));
    CHECK_FALSE(machine.failed());

    /* What a file gets: the program's line, to the character. */
    CHECK(printed == "HELLO FROM THE FOLDER\n");

    /* The program's line and nothing of the monitor's: no command echoed,
     * no prompt after. */
    CHECK(screenText(machine.emulator()) == "HELLO FROM THE FOLDER\n");

    /* The folder is as it was: no descriptor, nothing of ours. */
    CHECK(filesIn(dir) == std::set<std::string>{"hello.sav"});
}

TEST_CASE("the rest of the command line is the program's") {
    const auto dir = freshDir("dir");
    fs::copy_file(fs::path(RT11_SYSTEM_DIR) / "DIR.SAV", dir / "dir.sav");
    writeFile(dir / "primer.mac", std::vector<uint8_t>(700, 'M'));
    writeFile(dir / "other.txt", std::vector<uint8_t>(10, 'T'));

    Machine machine;
    const std::vector<std::string> args{"*.mac"};
    REQUIRE(machine.start(dir / "dir.sav", args));
    REQUIRE(runToEnd(machine));
    CHECK_FALSE(machine.failed());

    /* DIR listed DK: - the folder - by the pattern, with the date the
     * state was saved at. */
    const std::string text = screenText(machine.emulator());
    CAPTURE(text);
    CHECK(text.find("31-Dec-99") != std::string::npos);
    CHECK(text.find("PRIMER.MAC") != std::string::npos);
    CHECK(text.find("OTHER") == std::string::npos);
    CHECK(text.find("DIR   .SAV") == std::string::npos);
    CHECK(text.find("1 Files, 2 Blocks") != std::string::npos);
}

TEST_CASE("an overlaid program reads its overlays and writes into the folder") {
    const auto dir = freshDir("pip");
    fs::copy_file(fs::path(RT11_SYSTEM_DIR) / "PIP.SAV", dir / "pip.sav");
    const std::string content = "copied through PIP\r\n";
    writeFile(dir / "source.txt", {content.begin(), content.end()});

    Machine machine;
    const std::vector<std::string> args{"copy.txt=source.txt"};
    REQUIRE(machine.start(dir / "pip.sav", args));
    REQUIRE(runToEnd(machine));
    CHECK_FALSE(machine.failed());
    CAPTURE(screenText(machine.emulator()));

    CHECK(filesIn(dir) == std::set<std::string>{"pip.sav", "source.txt", "copy.txt"});
    /* RT-11 files are whole blocks: the text, then zeros. */
    const std::string copy = readText(dir / "copy.txt");
    CHECK(copy.size() == 512);
    CHECK(copy.substr(0, content.size()) == content);
}

TEST_CASE("a program the monitor stops is over too, and has failed") {
    const auto dir = freshDir("trap");
    /* MUL: the processor has no EIS, a reserved instruction. */
    writeFile(dir / "trap.sav", program({0070001}));

    Machine machine;
    REQUIRE(machine.start(dir / "trap.sav", {}));
    std::string printed;
    REQUIRE(runToEnd(machine, &printed));
    CHECK(printed == "\n?MON-F-Trap to 10 001002\n");
    CHECK(machine.failed());
    CHECK(screenText(machine.emulator()).find("?MON-F-") != std::string::npos);
}

TEST_CASE("a utility that reports an error has failed") {
    const auto dir = freshDir("pip-error");
    fs::copy_file(fs::path(RT11_SYSTEM_DIR) / "PIP.SAV", dir / "pip.sav");

    Machine machine;
    const std::vector<std::string> args{"copy.txt=nosuch.txt"};
    REQUIRE(machine.start(dir / "pip.sav", args));
    std::string printed;
    REQUIRE(runToEnd(machine, &printed));
    CHECK(machine.failed());
    CHECK(printed == "?PIP-F-File not found DK:NOSUCH.TXT\n");
    CHECK(filesIn(dir) == std::set<std::string>{"pip.sav"});
}

TEST_CASE("what cannot be run is refused before the machine is touched") {
    const auto dir = freshDir("refused");
    writeFile(dir / "hello.sav", helloProgram("HI"));

    Machine machine;
    CHECK_FALSE(machine.start(dir / "missing.sav", {}));
    const std::vector<std::string> longArgs{std::string(100, 'A')};
    CHECK_FALSE(machine.start(dir / "hello.sav", longArgs));
    CHECK(ms0515::run::starterWaiting(machine.emulator()));

    std::string command;
    const std::vector<std::string> args{"out.txt=in.txt", "/b"};
    REQUIRE(ms0515::run::runCommand(command, "MACRO.SAV", args));
    CHECK(command == "RUN DK:MACRO.SAV OUT.TXT=IN.TXT /B");
}

}
