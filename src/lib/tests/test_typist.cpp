/*
 * test_typist.cpp - characters into keys: which key, with what held, and
 * a key at a time.
 */

#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Typist.hpp>

#include <string>
#include <vector>

namespace {

using ms0515::Key;

struct Seen {
    Key  key;
    bool shift, ctrl;
};

/* Type `koi8` and record each key as it is held, with its modifiers. */
std::vector<Seen> taps(ms0515::Typist &typist, const std::string &koi8)
{
    static constexpr Key candidates[] = {
        Key::A, Key::B, Key::C, Key::Digit1, Key::Return, Key::RusLat,
        Key::Backspace, Key::Up, Key::F1, Key::Period,
    };
    ms0515::Emulator emu;
    for (char c : koi8) typist.type(static_cast<uint8_t>(c));
    std::vector<Seen> seen;
    Key before = Key::None;
    for (int frame = 0; frame < 200 && typist.pending() > 0; ++frame) {
        typist.pump(emu);
        Key held = Key::None;
        for (Key k : candidates)
            if (emu.keyHeld(k)) held = k;
        if (held != Key::None && before == Key::None)   /* a key went down */
            seen.push_back({held, emu.keyHeld(Key::ShiftL), emu.keyHeld(Key::Ctrl)});
        before = held;
        (void)emu.stepFrame();
    }
    CHECK_FALSE(emu.keyHeld(Key::ShiftL));
    CHECK_FALSE(emu.keyHeld(Key::Ctrl));
    return seen;
}

}  /* namespace */

TEST_SUITE("Typist") {

TEST_CASE("letters, case, digits and the line end") {
    ms0515::Typist typist;
    /* In ЛАТ the bare key is the capital; Shift gives the small letter. */
    const auto seen = taps(typist, "Ab1\n");
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].key == Key::A);       CHECK_FALSE(seen[0].shift);
    CHECK(seen[1].key == Key::B);       CHECK(seen[1].shift);
    CHECK(seen[2].key == Key::Digit1);  CHECK_FALSE(seen[2].shift);
    CHECK(seen[3].key == Key::Return);
}

TEST_CASE("a control code is the letter with Ctrl held") {
    ms0515::Typist typist;
    const auto seen = taps(typist, "\003\010");     /* ^C, then Backspace */
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].key == Key::C);
    CHECK(seen[0].ctrl);
    CHECK(seen[1].key == Key::Backspace);
    CHECK_FALSE(seen[1].ctrl);
}

TEST_CASE("a Russian letter switches to РУС once, a Latin one back") {
    ms0515::Typist typist;
    /* KOI-8R: а = 0xC1, б = 0xC2 */
    typist.type(uint8_t{0xC1});
    typist.type(uint8_t{0xC2});
    CHECK(typist.pending() == 3);       /* РУС/ЛАТ, а, б */
    typist.type(uint8_t{'A'});
    CHECK(typist.pending() == 5);       /* РУС/ЛАТ, A */
    typist.type(uint8_t{'B'});
    typist.type(uint8_t{'1'});          /* a digit is the same in both */
    CHECK(typist.pending() == 7);

    /* The keyboard's own РУС/ЛАТ state follows: on after the first two
     * letters, off again at the end. */
    ms0515::Emulator emu;
    bool wasRus = false;
    for (int frame = 0; frame < 200 && typist.pending() > 0; ++frame) {
        typist.pump(emu);
        (void)emu.stepFrame();
        wasRus = wasRus || emu.ruslatOn();
    }
    CHECK(wasRus);
    CHECK_FALSE(emu.ruslatOn());
}

TEST_CASE("keys without a character, and characters without a key") {
    ms0515::Typist typist;
    typist.type(Key::Up);
    typist.type(Key::None);
    typist.type(uint8_t{'`'});          /* the keyboard has no such key */
    CHECK(typist.pending() == 1);
    const auto seen = taps(typist, "");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].key == Key::Up);
    CHECK(typist.pending() == 0);
}

}
