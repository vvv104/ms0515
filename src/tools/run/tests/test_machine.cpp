/*
 * test_machine.cpp - ms0515-run's machine, from what the binary carries to
 * a program run and over: the same calls main() makes, without a terminal
 * or a window.
 */

#include <doctest/doctest.h>

#include "ConsoleText.hpp"
#include "Embedded.hpp"
#include "HostKeys.hpp"
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
    const auto serial = ms0515::run::unpackZeroRuns(ms0515::run::embedded::disk);
    REQUIRE(serial.has_value());
    const auto volume = ms0515::disk::SparseVolume::parse(*serial);
    REQUIRE(volume.has_value());
    CHECK(volume->blocks() == 800);
    const auto opened = ms0515::disk::openLinearImage(volume->toLinear());
    REQUIRE(opened.has_value());

    /* It is carried as what is on it, not as a diskette's 400 KB: the
     * bound is there for growth to be seen. */
    CHECK(volume->held() <= 115);
    CHECK(ms0515::run::embedded::disk.size() < 60 * 1024);

    std::set<std::string> names;
    for (const auto &e : opened->directory.entries)
        if (e.isPermanent()) names.insert(e.name);
    CHECK(names == std::set<std::string>{"RT11SJ.SYS", "SWAP.SYS", "DZ.SYS",
                                         "TT.SYS", "HD.SYS", "EM.SYS",
                                         "START.SAV"});
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
    const std::vector<std::string> args{"primer.mac"};
    REQUIRE(machine.start(dir / "dir.sav", args));
    REQUIRE(runToEnd(machine));
    CHECK_FALSE(machine.failed());

    /* DIR listed the file named, with the date the state was saved at. */
    const std::string text = screenText(machine.emulator());
    CAPTURE(text);
    CHECK(text.find("31-Dec-99") != std::string::npos);
    CHECK(text.find("PRIMER.MAC") != std::string::npos);
    CHECK(text.find("OTHER") == std::string::npos);
    CHECK(text.find("DIR   .SAV") == std::string::npos);
    CHECK(text.find("1 Files, 2 Blocks") != std::string::npos);
}

TEST_CASE("DK: holds the program and the files the line names, nothing else") {
    const auto dir = freshDir("volume");
    fs::copy_file(fs::path(RT11_SYSTEM_DIR) / "DIR.SAV", dir / "DIR.SAV");
    writeFile(dir / "primer.mac", std::vector<uint8_t>(700, 'M'));
    writeFile(dir / "PRIMER.OBJ", std::vector<uint8_t>(100, 'O'));
    writeFile(dir / "other.txt", std::vector<uint8_t>(10, 'T'));
    writeFile(dir / "ms0515.exe", std::vector<uint8_t>(5000, 'E'));

    /* The program is found as the host finds it: where the host does not
     * tell dir.sav from DIR.SAV, neither spelling is refused. */
    const fs::path asTyped =
        fs::exists(dir / "dir.sav") ? dir / "dir.sav" : dir / "DIR.SAV";

    {   /* With no file named, the program alone. */
        Machine machine;
        const std::vector<std::string> args{"/b"};
        REQUIRE(machine.start(asTyped, args));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CAPTURE(printed);
        CHECK(printed.find("DIR   .SAV") != std::string::npos);
        CHECK(printed.find("MS0515") == std::string::npos);
        CHECK(printed.find("PRIMER") == std::string::npos);
    }
    {   /* A wildcard brings what it matches. */
        Machine machine;
        const std::vector<std::string> args{"*.*"};
        REQUIRE(machine.start(asTyped, args));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CAPTURE(printed);
        CHECK(printed.find("MS0515.EXE") != std::string::npos);
        CHECK(printed.find("OTHER .TXT") != std::string::npos);
        CHECK(printed.find(" 5 Files") != std::string::npos);
    }
    {   /* One with an extension, only that extension's. */
        Machine machine;
        const std::vector<std::string> args{"pr%mer.o*", "*.*"};
        REQUIRE(machine.start(asTyped, {args.data(), 1}));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CHECK(printed.find("PRIMER.OBJ") != std::string::npos);
        CHECK(printed.find(" 1 Files") != std::string::npos);
    }
    {   /* A name brings its files of every extension - and the program
         * itself may be named without its .SAV, as for RUN. */
        Machine machine;
        const std::vector<std::string> args{"primer/b"};
        REQUIRE(machine.start(dir / "DIR", args));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CAPTURE(printed);
        CHECK(printed.find("PRIMER.MAC") != std::string::npos);
        CHECK(printed.find("PRIMER.OBJ") != std::string::npos);
        CHECK(printed.find("OTHER") == std::string::npos);
        CHECK(printed.find("MS0515") == std::string::npos);
    }
    CHECK(filesIn(dir).size() == 5);
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
    /* JMP R0: no such instruction, on this processor or with EM. */
    writeFile(dir / "trap.sav", program({0000100}));

    Machine machine;
    REQUIRE(machine.start(dir / "trap.sav", {}));
    std::string printed;
    REQUIRE(runToEnd(machine, &printed));
    CHECK(printed == "\n?MON-F-Trap to 10 001002\n");
    CHECK(machine.failed());
    CHECK(screenText(machine.emulator()).find("?MON-F-") != std::string::npos);
}

namespace {

/* .LOOKUP a file on channel 0 and say whether it was there.  `device`
 * and the name DATA.BIN in RAD50. */
std::vector<uint8_t> lookupProgram(uint16_t device)
{
    return program({
        0012700, 01030,     /* 1000  MOV #AREA,R0          */
        0104375,            /* 1004  EMT 375               */
        0103404,            /* 1006  BCS 1020              */
        0012700, 01050,     /* 1010  MOV #FOUND,R0         */
        0104351,            /* 1014  .PRINT                */
        0104350,            /* 1016  .EXIT                 */
        0012700, 01056,     /* 1020  MOV #MISSING,R0       */
        0104351,            /* 1024  .PRINT                */
        0104350,            /* 1026  .EXIT                 */
        0000400, 01040, 0,  /* 1030  AREA: channel 0, .LOOKUP, the name */
        0,
        device, 0014474, 0003100, 0006766,      /* 1040  dev:DATA.BIN */
    }, std::string("FOUND\0MISSING\0", 14));
}

constexpr uint16_t kRad50Dk = 0015270, kRad50Sy = 0075250;

}  /* namespace */

TEST_CASE("a file the program asks for by name is given from its folder") {
    for (const uint16_t device : {kRad50Dk, kRad50Sy}) {
        CAPTURE(device);
        const auto dir = freshDir("asked");
        writeFile(dir / "ask.sav", lookupProgram(device));
        {
            Machine machine;                    /* not in the folder */
            REQUIRE(machine.start(dir / "ask.sav", {}));
            std::string printed;
            REQUIRE(runToEnd(machine, &printed));
            CHECK(printed == "MISSING\n");
        }
        writeFile(dir / "data.bin", std::vector<uint8_t>(1500, 'D'));
        {
            Machine machine;                    /* there, though not named */
            REQUIRE(machine.start(dir / "ask.sav", {}));
            std::string printed;
            REQUIRE(runToEnd(machine, &printed));
            CHECK(printed == "FOUND\n");
        }
        CHECK(filesIn(dir) == std::set<std::string>{"ask.sav", "data.bin"});
        CHECK(fs::file_size(dir / "data.bin") == 1500);
    }
}

/* The games of the RT-11 development tree, when they are built there
 * (their data files are not in the repository): graphics programs that
 * read a data file of their own and never come back to the monitor. */
TEST_CASE("a graphics game starts the same way and takes the screen") {
    const fs::path projects = fs::path(RT11_SYSTEM_DIR) / ".." / ".." / "projects";
    for (const char *game : {"manicm/MANICM", "saper/SAPER"}) {
        const fs::path sav = projects / (std::string{game} + ".SAV");
        const fs::path dat = projects / (std::string{game} + ".DAT");
        if (!fs::exists(sav) || !fs::exists(dat)) continue;
        const std::string name = sav.stem().string();
        CAPTURE(name);
        const auto dir = freshDir(name.c_str());
        for (const char *extension : {".SAV", ".DAT", ".HLP"}) {
            const fs::path file = projects / (std::string{game} + extension);
            if (fs::exists(file)) fs::copy_file(file, dir / file.filename());
        }

        Machine machine;
        REQUIRE(machine.start(dir / sav.filename(), {}));
        std::string printed;
        for (int f = 0; f < 1500 && machine.step(); ++f)
            printed += machine.takeOutput();
        CAPTURE(printed);
        CHECK_FALSE(machine.ended());
        CHECK(machine.interactive());

        /* It drew: the picture is kept beside the fixture for the eye
         * (MANICM in the colour mode, SAPER in the console's 640x200). */
        auto &emu = machine.emulator();
        int lit = 0;
        std::ofstream ppm(dir / "screen.ppm", std::ios::binary);
        ppm << "P6\n" << (emu.isHires() ? 640 : 320) << " 200\n255\n";
        emu.forEachLoResPixel([&](int, int, bool on, const ms0515::LoResAttr &a) {
            lit += on;
            const uint8_t grb = on ? a.fgGrb : a.bgGrb;
            const char level = static_cast<char>(a.bright ? 255 : 170);
            const char rgb[3] = {grb & 2 ? level : '\0', grb & 4 ? level : '\0',
                                 grb & 1 ? level : '\0'};
            ppm.write(rgb, 3);
        });
        emu.forEachHiResPixel([&](int, int, bool on) {
            lit += on;
            const char rgb[3] = {on ? '\xFF' : '\0', on ? '\xFF' : '\0',
                                 on ? '\xFF' : '\0'};
            ppm.write(rgb, 3);
        });
        CHECK(lit > 1000);
    }
}

TEST_CASE("a program that waits for a key is interactive, and gets the key typed") {
    const auto dir = freshDir("key");
    /* .TTYIN in the single-character mode (JSW bit 12), then the key
     * back out and .EXIT. */
    auto image = program({
        0104340,            /* 1000  EMT 340   .TTYIN       */
        0103776,            /* 1002  BCS 1000               */
        0104341,            /* 1004  EMT 341   .TTYOUT      */
        0104350,            /* 1006  .EXIT                  */
    });
    image[045] = 0x10;
    writeFile(dir / "key.sav", image);

    Machine machine;
    REQUIRE(machine.start(dir / "key.sav", {}));
    for (int f = 0; f < 300 && machine.step(); ++f) {}
    REQUIRE_FALSE(machine.ended());
    CHECK(machine.interactive());
    CHECK(machine.waitingForKey());

    ms0515::run::HostKeys keys;
    ms0515::Typist typist;
    const std::string typed = "Q";
    CHECK_FALSE(keys.feed({reinterpret_cast<const uint8_t *>(typed.data()),
                           typed.size()}, typist));
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    for (int f = 0; f < 2000 && !machine.ended(); ++f) {
        typist.pump(machine.emulator());
        machine.step();
        printed += text.convert(machine.takeOutput());
    }
    REQUIRE(machine.ended());
    printed += text.convert(machine.drainOutput());
    CHECK(printed == "Q");
    CHECK_FALSE(machine.failed());
}

TEST_CASE("a program that reads the keyboard past the monitor is interactive too") {
    const auto dir = freshDir("romkey");
    /* The ROM's own entries: the key input until there is a key (C set
     * when none), the character output, .EXIT - with interrupts shut
     * out, or the monitor's keyboard interrupt would have the key first.
     * Nothing asked of the monitor, nothing drawn, no sound. */
    writeFile(dir / "romkey.sav", program({
        0106427, 0000340,   /* 1000  MTPS #340               */
        0004737, 0160004,   /* 1004  CALL @#160004           */
        0103775,            /* 1010  BCS 1004                */
        0004737, 0160000,   /* 1012  CALL @#160000           */
        0104350,            /* 1016  .EXIT                   */
    }));

    Machine machine;
    REQUIRE(machine.start(dir / "romkey.sav", {}));
    for (int f = 0; f < 300 && machine.step(); ++f) {}
    REQUIRE_FALSE(machine.ended());
    CHECK(machine.interactive());
    CHECK(machine.takesKeys());
    CHECK_FALSE(machine.graphics());

    ms0515::Typist typist;
    typist.type(uint8_t{'W'});
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    for (int f = 0; f < 2000 && !machine.ended(); ++f) {
        if (machine.takesKeys()) typist.pump(machine.emulator());
        machine.step();
        printed += text.convert(machine.takeOutput());
    }
    REQUIRE(machine.ended());
    printed += text.convert(machine.drainOutput());
    CHECK(printed == "W");
}

TEST_CASE("a program that takes the keyboard's interrupt is interactive") {
    const auto dir = freshDir("ownkbd");
    writeFile(dir / "ownkbd.sav", program({
        0012737, 01010, 0130,   /* 1000  MOV #HANDLER,@#130  */
        0000777,                /* 1006  BR .                */
        0000002,                /* 1010  HANDLER: RTI        */
    }));

    Machine machine;
    REQUIRE(machine.start(dir / "ownkbd.sav", {}));
    for (int f = 0; f < 300 && machine.step(); ++f) {}
    REQUIRE_FALSE(machine.ended());
    CHECK(machine.interactive());
    CHECK(machine.takesKeys());
}

TEST_CASE("a program that does its work and ends is not interactive") {
    const auto dir = freshDir("batch");
    fs::copy_file(fs::path(RT11_SYSTEM_DIR) / "PIP.SAV", dir / "pip.sav");
    writeFile(dir / "source.txt", std::vector<uint8_t>(3000, 'S'));

    Machine machine;
    const std::vector<std::string> args{"copy.txt=source.txt"};
    REQUIRE(machine.start(dir / "pip.sav", args));
    REQUIRE(runToEnd(machine));
    CHECK_FALSE(machine.interactive());
    CHECK_FALSE(machine.waitingForKey());
}

TEST_CASE("the bell is the keyboard's and makes no program interactive") {
    /* ^G rings the MS-7004, a box of its own that the tool does not play;
     * the machine's speaker stays silent, and the run stays unthrottled. */
    const auto dir = freshDir("bell");
    writeFile(dir / "bell.sav", helloProgram("A\007\007\007B"));

    Machine machine;
    REQUIRE(machine.start(dir / "bell.sav", {}));
    std::string printed;
    REQUIRE(runToEnd(machine, &printed));
    CHECK(printed == "AB\n");
    CHECK_FALSE(machine.interactive());
}

TEST_CASE("the host's bytes become characters and keys") {
    ms0515::run::HostKeys keys;
    ms0515::Typist typist;
    const auto feed = [&](const std::string &bytes) {
        return keys.feed({reinterpret_cast<const uint8_t *>(bytes.data()),
                          bytes.size()}, typist);
    };

    CHECK_FALSE(feed("DIR\r"));
    CHECK(typist.pending() == 4);
    CHECK_FALSE(feed("\033[A\033OB"));              /* two arrows */
    CHECK(typist.pending() == 6);
    CHECK_FALSE(feed("\033[1;5H\033[2~"));          /* sequences it does not know */
    CHECK(typist.pending() == 6);
    CHECK_FALSE(feed("\xD0"));                      /* a letter cut in two: д */
    CHECK(typist.pending() == 6);
    CHECK_FALSE(feed("\xB4"));
    CHECK(typist.pending() == 8);                   /* РУС/ЛАТ and the letter */
    CHECK(feed("A\035" "B"));                       /* Ctrl-] is the host's */
    CHECK(typist.pending() == 11);                  /* РУС/ЛАТ, A, B */
}

TEST_CASE("a file a program opened and never closed is not left in the folder") {
    const auto dir = freshDir("unclosed");
    /* .ENTER DK:JUNK.TMP on channel 0 with the size left to the monitor
     * (half the largest free area), then .EXIT with the file open: on
     * RT-11 such a file is gone. */
    writeFile(dir / "enter.sav", program({
        0012700, 01010,     /* MOV #AREA,R0             */
        0104375,            /* EMT 375                  */
        0104350,            /* .EXIT                    */
        0001000,            /* AREA: channel 0, .ENTER  */
        0001020,            /*       the file's name    */
        0, 0,               /*       size, sequence     */
        0015270, 0040726, 0042300, 0077430,     /* .RAD50 /DK JUNK  TMP/ */
    }));
    {
        Machine machine;
        REQUIRE(machine.start(dir / "enter.sav", {}));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CHECK(printed.empty());
    }
    CHECK(filesIn(dir) == std::set<std::string>{"enter.sav"});
}

TEST_CASE("the instructions the processor lacks are emulated when asked for") {
    /* The machine has no EIS; EM, on the system diskette and switched on
     * for the run that asks, does MUL for a program built for a PDP-11
     * that has it.  Unasked it is off, and the program is stopped as on
     * the machine itself. */
    const auto dir = freshDir("eis");
    writeFile(dir / "mul.sav", program({
        0012701, 5,         /* MOV  #5,R1          */
        0070127, 3,         /* MUL  #3,R1          */
        0062701, 060,       /* ADD  #60,R1    15 + '0' = '?' */
        0010100,            /* MOV  R1,R0          */
        0104341,            /* .TTYOUT             */
        0104350,            /* .EXIT               */
    }));

    {
        Machine machine;
        Machine::Options options;
        options.instructionEmulator = true;
        REQUIRE(machine.start(dir / "mul.sav", {}, options));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CHECK(printed == "?");
        CHECK_FALSE(machine.failed());
    }
    {
        Machine machine;
        REQUIRE(machine.start(dir / "mul.sav", {}));
        std::string printed;
        REQUIRE(runToEnd(machine, &printed));
        CHECK(printed.find("?MON-F-Trap to 10") != std::string::npos);
        CHECK(machine.failed());
    }
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
