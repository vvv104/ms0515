/*
 * test_programs.cpp - the example programs, run and read.
 */

#include "Programs.hpp"

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
