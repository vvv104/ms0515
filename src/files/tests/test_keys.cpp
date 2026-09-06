/*
 * test_keys.cpp — the function keys with a modifier, as the terminal
 * sends them: xterm's CSI forms, which FTXUI passes through unnamed.
 */
#include "Keys.hpp"

#include <doctest/doctest.h>

using namespace ms0515::files;

TEST_CASE("Alt+F1..F4 and Alt+F5..F12 in xterm's CSI form")
{
    const auto f1 = parseFunctionKey("\x1B[1;3P");
    REQUIRE(f1.has_value());
    CHECK(f1->number == 1);
    CHECK(f1->alt);
    CHECK_FALSE(f1->shift);
    CHECK_FALSE(f1->ctrl);
    CHECK(parseFunctionKey("\x1B[1;3Q")->number == 2);
    CHECK(parseFunctionKey("\x1B[1;3R")->number == 3);
    CHECK(parseFunctionKey("\x1B[1;3S")->number == 4);
    CHECK(parseFunctionKey("\x1B[15;3~")->number == 5);
    CHECK(parseFunctionKey("\x1B[17;3~")->number == 6);
    CHECK(parseFunctionKey("\x1B[18;3~")->number == 7);
    CHECK(parseFunctionKey("\x1B[19;3~")->number == 8);
    CHECK(parseFunctionKey("\x1B[20;3~")->number == 9);
    CHECK(parseFunctionKey("\x1B[21;3~")->number == 10);
    CHECK(parseFunctionKey("\x1B[23;3~")->number == 11);
    CHECK(parseFunctionKey("\x1B[24;3~")->number == 12);
}

TEST_CASE("the modifier bits: 2 shift, 3 alt, 5 ctrl, 4 shift+alt, and the ESC-prefixed alt of other terminals")
{
    const auto shift = parseFunctionKey("\x1B[1;2P");
    REQUIRE(shift.has_value());
    CHECK(shift->shift);
    CHECK_FALSE(shift->alt);
    const auto ctrl = parseFunctionKey("\x1B[15;5~");
    REQUIRE(ctrl.has_value());
    CHECK(ctrl->ctrl);
    CHECK(ctrl->number == 5);
    const auto both = parseFunctionKey("\x1B[1;4Q");
    REQUIRE(both.has_value());
    CHECK(both->shift);
    CHECK(both->alt);
    const auto escAlt = parseFunctionKey("\x1B\x1BOP");
    REQUIRE(escAlt.has_value());
    CHECK(escAlt->alt);
    CHECK(escAlt->number == 1);
    const auto escAltTilde = parseFunctionKey("\x1B\x1B[15~");
    REQUIRE(escAltTilde.has_value());
    CHECK(escAltTilde->alt);
    CHECK(escAltTilde->number == 5);
}

TEST_CASE("plain function keys and everything else")
{
    const auto plain = parseFunctionKey("\x1BOP");
    REQUIRE(plain.has_value());
    CHECK(plain->number == 1);
    CHECK_FALSE(plain->alt);
    CHECK(parseFunctionKey("\x1B[15~")->number == 5);
    CHECK(parseFunctionKey("\x1B[21~")->number == 10);
    CHECK_FALSE(parseFunctionKey("a").has_value());
    CHECK_FALSE(parseFunctionKey("").has_value());
    CHECK_FALSE(parseFunctionKey("\x1B[A").has_value());          /* an arrow */
    CHECK_FALSE(parseFunctionKey("\x1B[1;3A").has_value());       /* Alt+arrow */
    CHECK_FALSE(parseFunctionKey("\x1B[16;3~").has_value());      /* no such key */
    CHECK_FALSE(parseFunctionKey("\x1B[1;3").has_value());        /* cut short */
}
