#include <doctest/doctest.h>

#include "ConsoleText.hpp"

#include <string>

using ms0515::run::ConsoleText;

TEST_SUITE("ConsoleText") {

TEST_CASE("a file gets the text and its line ends, nothing else") {
    ConsoleText text(ConsoleText::Reader::plain);
    CHECK(text.convert("HELLO\r\nWORLD\r\n") == "HELLO\nWORLD\n");
    CHECK(text.convert("A\tB\007\010C") == "A\tBC");
    CHECK(text.convert("\033H\033JCLEAN") == "CLEAN");
    CHECK(text.convert("\033Y\040\041X") == "X");
    CHECK(text.convert("\016RUS\017LAT") == "RUSLAT");
}

TEST_CASE("the machine's letters and lines come as UTF-8") {
    ConsoleText text(ConsoleText::Reader::plain);
    /* KOI-8R: 0xF0 0xD2 0xC9 0xD7 0xC5 0xD4 */
    CHECK(text.convert("\xF0\xD2\xC9\xD7\xC5\xD4") == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    /* ROM-B's own pseudographics: 240 is the single corner, 244 the line. */
    CHECK(text.convert("\xA0\xA4") == "\xE2\x94\x8C\xE2\x94\x80");
}

TEST_CASE("a terminal keeps the cursor movement, as ANSI") {
    ConsoleText text(ConsoleText::Reader::terminal);
    CHECK(text.convert("A\r\nB\010") == "A\r\nB\010");
    CHECK(text.convert("\033H\033J") == "\033[H\033[J");
    CHECK(text.convert("\033K\033A") == "\033[K\033[A");
    CHECK(text.convert("\033Y\042\050X") == "\033[3;9HX");   /* row 2, column 8, from 0 */
    CHECK(text.convert("\033?") == "");                      /* not one it knows */
}

TEST_CASE("an ANSI sequence is the terminal's as it is, and nothing to a file") {
    /* What K52 and KED open with, then text. */
    ConsoleText plain(ConsoleText::Reader::plain);
    CHECK(plain.convert("\033[?2lfirst\033[12;40Hsecond\033[K") == "firstsecond");

    ConsoleText terminal(ConsoleText::Reader::terminal);
    CHECK(terminal.convert("\033[?2lfirst\033[12;40Hsecond\033[K") ==
          "\033[?2lfirst\033[12;40Hsecond\033[K");
    CHECK(terminal.convert("\033[1") == "");            /* cut */
    CHECK(terminal.convert(";2Hx") == "\033[1;2Hx");
}

TEST_CASE("a sequence cut between two calls is finished by the next") {
    ConsoleText text(ConsoleText::Reader::terminal);
    CHECK(text.convert("AB\033") == "AB");
    CHECK(text.convert("HCD") == "\033[HCD");
    CHECK(text.convert("\033Y") == "");
    CHECK(text.convert("\040") == "");
    CHECK(text.convert("\040Z") == "\033[1;1HZ");
}

}
