/*
 * test_programs.cpp - the example programs, run and read.
 */

#include "Programs.hpp"

#include "EmulatorInternal.hpp"
#include <ms0515/Typist.hpp>

extern "C" {
#include <ms0515/core/board.h>
}

#include <cstdint>
#include <sstream>
#include <string>

using namespace gcc_tests;

namespace {

/* ARITH's table (examples/arith.c), and what C says of each pair on a
 * 16-bit machine: the quotient truncated toward zero, the remainder with
 * the dividend's sign, the product's low 16 bits - and the unsigned
 * quotient and remainder of the same bits. */
constexpr int16_t kPairs[][2] = {
    {7, 2}, {-7, 2}, {7, -2}, {-7, -2},
    {32767, 1}, {-32768, 1}, {-32768, -1}, {0, 5}, {1, 32767},
    {30000, 7}, {-30000, 7}, {12345, -123}, {-32768, 2}, {32767, 32767},
    {255, 256}, {-1, 1}, {-1, 2}, {-1, -1}, {1000, 1000}, {4096, 64},
};

std::string expectedArith(int16_t a, int16_t b)
{
    const auto wrap = [](long v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); };
    const long la = a, lb = b;
    const auto ua = static_cast<uint16_t>(a), ub = static_cast<uint16_t>(b);
    std::ostringstream s;
    s << a << ' ' << b << ": " << wrap(la / lb) << ' ' << wrap(la % lb) << ' ' << wrap(la * lb)
      << ' ' << ua / ub << ' ' << ua % ub;
    return s.str();
}

}  // namespace

TEST_SUITE("gcc toolset programs") {

TEST_CASE("HELLO prints its line by .PRINT and comes back to the monitor") {
    if (!built("HELLO")) { MESSAGE("HELLO.SAV not built - skipped"); return; }
    const Run r = run("HELLO");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    CHECK(r.printed == "HELLO FROM GCC\n");
}

TEST_CASE("ARITH: the 16-bit helpers agree with C on every pair") {
    if (!built("ARITH")) { MESSAGE("ARITH.SAV not built - skipped"); return; }
    const Run r = run("ARITH");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const auto got = lines(r.printed);
    constexpr std::size_t n = sizeof kPairs / sizeof kPairs[0];
    REQUIRE(got.size() == n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        CHECK(got[i] == expectedArith(kPairs[i][0], kPairs[i][1]));
    }
    CHECK(got[n] == "ARITH DONE");
}

TEST_CASE("LIBC: printf, strings, ctype, stdlib and the heap, a line a group") {
    if (!built("LIBC")) { MESSAGE("LIBC.SAV not built - skipped"); return; }
    const Run r = run("LIBC");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const std::vector<std::string> expected = {
        "42|-42|65535|ff|FF|10|c|str|%",
        "[   42][42   ][00042][ab][   ab][ab   ]",
        "123456|-123456|4294967295|abcdef",
        "-32768|32767|65535",
        "864192 -300000 4294836225 142857",
        "7-x",
        "abc 6",
        "5 -1 foobar 2 3 0 aabcde xxxyz -1 0",
        "1 1 A q 1",
        "-123 42 5 511 -31 []",
        "1 1 1 1",
        "1 1",
        "LIBC DONE",
    };
    const auto got = lines(r.printed);
    REQUIRE(got.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == expected[i]);
    }
}

/* GCC 15.2's pdp11 backend compares longs wrongly when the high words
 * are equal; build-toolchain.sh patches it (toolset/gcc/README.md, "A
 * trap in the compiler").  This fails on a compiler without the patch. */
TEST_CASE("CMPLONG: signed comparisons of longs, the compiler's fix in place") {
    if (!built("CMPLONG")) { MESSAGE("CMPLONG.SAV not built - skipped"); return; }
    const Run r = run("CMPLONG");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    constexpr long kLongPairs[][2] = {
        {100000L, 5L}, {-100000L, 5L},
        {32768L, 5L}, {40000L, 32767L}, {-32768L, -40000L},
        {32768L, 0L}, {0x10000L + 32768L, 0x10000L + 5L},
        {5L, 32768L}, {-5L, -32768L}, {32768L, 32768L},
    };
    const auto got = lines(r.printed);
    constexpr std::size_t n = sizeof kLongPairs / sizeof kLongPairs[0];
    REQUIRE(got.size() == n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        const long x = kLongPairs[i][0], y = kLongPairs[i][1];
        std::string expected;
        for (bool b : {x > y, x < y, x >= y, x <= y, x >= 0, x < 0}) expected += b ? '1' : '0';
        CAPTURE(i);
        CHECK(got[i] == expected);
    }
    CHECK(got[n] == "CMPLONG DONE");
}

/* MACHINE (examples/machine.c) over the machine library: the screen in
 * colour with a diagonal and one cell's attribute, the clock counting
 * while it waits, the keyboard taken until Q, the console back. */
TEST_CASE("MACHINE: the screen, the clock and the keyboard through ms0515.h") {
    if (!built("MACHINE")) { MESSAGE("MACHINE.SAV not built - skipped"); return; }
    const fs::path dir = fs::temp_directory_path() / "ms0515_gcc_machine";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(savDir() / "MACHINE.SAV", dir / "MACHINE.SAV");
    ms0515::run::Machine machine;
    REQUIRE(machine.start(dir / "MACHINE.SAV", {}));
    ms0515::Typist typist;
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    const auto step = [&] { typist.pump(machine.emulator()); machine.step(); printed += text.convert(machine.takeOutput()); };
    for (int f = 0; f < 60; ++f) step();
    REQUIRE_FALSE(machine.ended());
    CHECK_FALSE(machine.emulator().isHires());

    const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
    const auto pixel = [&](int x, int y) { return (vram[y * 80 + (x >> 3) * 2] >> (7 - (x & 7))) & 1; };
    CHECK(pixel(0, 0) == 0);                /* unplotted */
    CHECK(pixel(8, 5) == 1);                /* x * 5 / 8 */
    CHECK(pixel(160, 100) == 1);
    CHECK(pixel(319, 199) == 1);
    CHECK(pixel(100, 10) == 0);
    CHECK(vram[1] == 0x07);                 /* the attribute asked: ink white */
    CHECK(vram[3 * 80 + 2 * 2 + 1] == 0x42);  /* cell (2, 3): ink red, bright */

    typist.type('q');
    for (int f = 0; f < 200 && !machine.ended(); ++f) step();
    REQUIRE(machine.ended());
    CHECK_FALSE(machine.failed());
    CHECK(machine.emulator().isHires());
    printed += text.convert(machine.drainOutput());
    unsigned frames = 0;
    int codes = 0;
    std::istringstream line(printed);
    std::string word, framesWord;
    REQUIRE((line >> word >> frames >> framesWord >> codes));
    CHECK(word == "MACHINE:");
    CHECK(frames >= 25);
    CHECK(frames < 300);
    CHECK(codes >= 1);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("CALC counts the primes below 10000 and tells its time") {
    if (!built("CALC")) { MESSAGE("CALC.SAV not built - skipped"); return; }
    const Run r = run("CALC");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const auto got = lines(r.printed);
    REQUIRE(got.size() == 2);
    CHECK(got[0].rfind("TIME: ", 0) == 0);
    MESSAGE("CALC " << got[0]);
    CHECK(got[1] == "PRIMES BELOW 10000: 1229 LAST 9973");
}

}
