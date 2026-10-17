/*
 * test_rt11.cpp - RT-11 programs on the machine: /bin/rt11 PROGRAM.SAV, a
 * program that owns the machine, puts its little monitor at 140000, where
 * RT-11's RMON is, loads the .SAV under it and serves its EMTs - the
 * console, .SETTOP, the files of the .SAV's own directory read, .EXIT a
 * reset.  The image is rt11test.dsk: the system with /rt11 holding the GCC
 * toolset's examples when they are built, and SABOT2 when the recovery
 * corpus has it; what is not there is skipped.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <ms0515/core/cpu.h>

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

/* Whether the test image holds the file. */
bool has(const char *path)
{
    const auto image = unixos::readRt11Image();
    if (image.empty()) return false;
    return Volume(image).lookup(path).has_value();
}

/* The program's end: its screen kept until a key, then a reset - the
 * screen gone and the system booted. */
void bootsAgain(Machine &m, const std::string &mark)
{
    REQUIRE(m.runUntil("rt11: the end", 3000));
    m.typeAndSettle("q", 0);
    int frames = 0;
    for (; frames < 3000 && m.screen().find(mark) != std::string::npos; frames += 10) m.run(10);
    CHECK(frames < 3000);
    REQUIRE(m.boot(3000));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("rt11 runs HELLO.SAV: .PRINT on the console, .EXIT a reset and the system again") {
    if (!has("/rt11/hello.sav")) { MESSAGE("no HELLO.SAV in rt11test.dsk - skipped"); return; }
    Machine m(unixos::readRt11Image(), false);
    atPrompt(m);
    m.typeAndSettle("rt11 /rt11/hello.sav\r", 0);
    REQUIRE(m.runUntil("HELLO FROM GCC", 3000));
    bootsAgain(m, "HELLO FROM GCC");
}

TEST_CASE("rt11 runs ARITH.SAV: the toolset's arithmetic on its own, line after line by .TTYOUT") {
    if (!has("/rt11/arith.sav")) { MESSAGE("no ARITH.SAV in rt11test.dsk - skipped"); return; }
    Machine m(unixos::readRt11Image(), false);
    atPrompt(m);
    m.typeAndSettle("rt11 /rt11/arith.sav\r", 0);
    REQUIRE(m.runUntil("ARITH DONE", 3000));
    INFO(m.screen());
    CHECK(m.screen().find("1000 1000: 1 0 16960 1 0") != std::string::npos);
}

TEST_CASE("rt11 runs INPUT.SAV: a line typed, echoed, read by .TTYIN; a key by itself") {
    if (!has("/rt11/input.sav")) { MESSAGE("no INPUT.SAV in rt11test.dsk - skipped"); return; }
    Machine m(unixos::readRt11Image(), false);
    atPrompt(m);
    m.typeAndSettle("rt11 /rt11/input.sav\r", 0);
    REQUIRE(m.runUntil("TYPE A LINE", 3000));
    m.typeAndSettle("Hello, 123\r", 60);
    REQUIRE(m.runUntil("INPUT 10:321 ,olleH", 600));
    m.typeAndSettle("z\r", 60);
    REQUIRE(m.runUntil("INPUT DONE", 600));
    INFO(m.screen());
    CHECK(m.screen().find("Hello, 123") != std::string::npos);      /* the echo */
    CHECK(m.screen().find("TTYIN 122") != std::string::npos);
}

TEST_CASE("rt11 refuses a .SAV that is not there, and the end is a reset") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    m.typeAndSettle("rt11 /rt11/none.sav\r", 0);
    REQUIRE(m.runUntil("rt11: /rt11/none.sav: not found", 3000));
    m.typeAndSettle("q", 0);
    int frames = 0;
    for (; frames < 3000 && m.screen().find("not found") != std::string::npos; frames += 10) m.run(10);
    CHECK(frames < 3000);
    REQUIRE(m.boot(3000));
}

TEST_CASE("rt11 runs SABOT2: its loader finds SABOT2.DAT on DZ0: and the game takes the machine") {
    if (!has("/rt11/sabot2.sav")) { MESSAGE("no SABOT2 in rt11test.dsk - skipped"); return; }
    Machine m(unixos::readRt11Image(), false);
    atPrompt(m);
    REQUIRE(ms0515::internal::board(m.emulator()).hires_mode);    /* the console's 640 x 200 */
    m.typeAndSettle("rt11 /rt11/sabot2.sav\r", 0);
    m.run(1500);
    INFO(m.screen());
    CHECK(m.screen().find("NAJDEN") == std::string::npos);     /* its loader's "not found" */
    CHECK(m.screen().find("\n#") == std::string::npos);        /* not the system again */
    /* The game's own: the screen 320 x 200, as its loader sets it once it
     * has the data, and the machine not halted. */
    CHECK_FALSE(ms0515::internal::board(m.emulator()).hires_mode);
    CHECK_FALSE(ms0515::internal::board(m.emulator()).cpu.halted);
}

}
