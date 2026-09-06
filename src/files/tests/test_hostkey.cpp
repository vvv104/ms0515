/*
 * test_hostkey.cpp — the terminal's byte stream turned into keys: plain
 * bytes, the ESC sequences of arrows, F-keys and the editing keys, the
 * Alt-modified F-keys, and a lone Esc.
 */
#include "HostKey.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ms0515::files;

namespace {

std::vector<HostKey> parse(const std::string &bytes, bool flushAtEnd = true)
{
    KeyParser p;
    std::vector<HostKey> out;
    for (const char c : bytes) {
        const auto keys = p.feed(static_cast<uint8_t>(c));
        out.insert(out.end(), keys.begin(), keys.end());
    }
    if (flushAtEnd) {
        const auto keys = p.flush();
        out.insert(out.end(), keys.begin(), keys.end());
    }
    return out;
}

} // namespace

TEST_CASE("plain bytes come out one key each, controls included")
{
    const auto keys = parse("a\x0D\x08\x03\x1C");
    REQUIRE(keys.size() == 5);
    CHECK(keys[0] == HostKey::ofByte('a'));
    CHECK(keys[1] == HostKey::ofByte(0x0D));
    CHECK(keys[2] == HostKey::ofByte(0x08));
    CHECK(keys[3] == HostKey::ofByte(0x03));
    CHECK(keys[4] == HostKey::ofByte(0x1C));
    CHECK(keys[0].isByte());
    CHECK_FALSE(keys[0].isSpecial());
}

TEST_CASE("arrows, home and end in their CSI and SS3 forms, Shift+Tab")
{
    const auto keys = parse("\x1B[A\x1B[B\x1B[C\x1B[D\x1B[H\x1B[F\x1BOH\x1BOF\x1B[Z");
    const std::vector<SpecialKey> want = {SpecialKey::up, SpecialKey::down, SpecialKey::right, SpecialKey::left,
                                          SpecialKey::home, SpecialKey::end, SpecialKey::home, SpecialKey::end,
                                          SpecialKey::backTab};
    REQUIRE(keys.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) CHECK(keys[i] == HostKey::ofSpecial(want[i]));
}

TEST_CASE("the tilde sequences: editing keys and F1..F12, and the SS3 F1..F4")
{
    const auto keys = parse("\x1B[1~\x1B[2~\x1B[3~\x1B[4~\x1B[5~\x1B[6~\x1B[7~\x1B[8~"
                            "\x1B[11~\x1B[12~\x1B[13~\x1B[14~\x1B[15~\x1B[17~\x1B[18~\x1B[19~\x1B[20~\x1B[21~\x1B[23~\x1B[24~"
                            "\x1BOP\x1BOQ\x1BOR\x1BOS");
    const std::vector<SpecialKey> want = {
        SpecialKey::home, SpecialKey::insert, SpecialKey::del, SpecialKey::end, SpecialKey::pageUp, SpecialKey::pageDown,
        SpecialKey::home, SpecialKey::end,
        SpecialKey::f1, SpecialKey::f2, SpecialKey::f3, SpecialKey::f4, SpecialKey::f5, SpecialKey::f6, SpecialKey::f7,
        SpecialKey::f8, SpecialKey::f9, SpecialKey::f10, SpecialKey::f11, SpecialKey::f12,
        SpecialKey::f1, SpecialKey::f2, SpecialKey::f3, SpecialKey::f4};
    REQUIRE(keys.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) CHECK(keys[i] == HostKey::ofSpecial(want[i]));
    CHECK(functionNumber(SpecialKey::f1) == 1);
    CHECK(functionNumber(SpecialKey::f12) == 12);
    CHECK(functionNumber(SpecialKey::home) == 0);
}

TEST_CASE("modified sequences: Alt+F1 (CSI 1;3P), Alt+F5 (CSI 15;3~), the ESC-prefixed Alt, and a modified arrow stays an arrow")
{
    const auto keys = parse("\x1B[1;3P\x1B[15;3~\x1B\x1BOQ\x1B[1;5A\x1B[1;2Q");
    REQUIRE(keys.size() == 5);
    CHECK(keys[0] == HostKey::ofSpecial(SpecialKey::f1, true));
    CHECK(keys[1] == HostKey::ofSpecial(SpecialKey::f5, true));
    CHECK(keys[2] == HostKey::ofSpecial(SpecialKey::f2, true));
    CHECK(keys[3] == HostKey::ofSpecial(SpecialKey::up));
    CHECK(keys[4] == HostKey::ofSpecial(SpecialKey::f2));   /* Shift is not Alt */
}

TEST_CASE("a lone Esc is a key once the burst ends; an unknown sequence falls through as bytes; a split sequence waits")
{
    KeyParser p;
    CHECK(p.feed(0x1B).empty());                 /* nothing yet: might be a sequence */
    const auto lone = p.flush();
    REQUIRE(lone.size() == 1);
    CHECK(lone[0] == HostKey::ofByte(0x1B));

    /* ESC followed by a plain letter: both, in order */
    const auto escX = parse("\x1Bx");
    REQUIRE(escX.size() == 2);
    CHECK(escX[0] == HostKey::ofByte(0x1B));
    CHECK(escX[1] == HostKey::ofByte('x'));

    /* an unknown CSI: the bytes as they came */
    const auto unknown = parse("\x1B[9x");
    REQUIRE(unknown.size() == 4);
    CHECK(unknown[0] == HostKey::ofByte(0x1B));
    CHECK(unknown[1] == HostKey::ofByte('['));
    CHECK(unknown[2] == HostKey::ofByte('9'));
    CHECK(unknown[3] == HostKey::ofByte('x'));

    /* "ESC [" at the end of a burst is a sequence in flight, not an Esc */
    KeyParser split;
    CHECK(split.feed(0x1B).empty());
    CHECK(split.feed('[').empty());
    CHECK(split.flush().empty());
    const auto rest = split.feed('A');
    REQUIRE(rest.size() == 1);
    CHECK(rest[0] == HostKey::ofSpecial(SpecialKey::up));
}
