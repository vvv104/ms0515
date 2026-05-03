/*
 * test_keyboard_layout.cpp — KeyboardLayout parser + Key bindings.
 *
 * Loads the actual on-screen keyboard layout shipped under
 * `assets/keyboard/ms7004_layout.txt` and verifies that every section
 * lands as a row, that load-bearing caps map to the expected
 * `ms0515::Key`, and that the mode-dependent letter/symbol predicates
 * agree with the cap-spec the OSK relies on.
 */

#include <doctest/doctest.h>

#include <ms0515/KeyboardLayout.hpp>
#include <ms0515/Emulator.hpp>      /* ms0515::Key */

#include <algorithm>
#include <string>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

const ms0515::KeyboardLayout::Cap *
findByLabel(const ms0515::KeyboardLayout &layout, std::string_view label)
{
    for (const auto &row : layout.rows())
        for (const auto &cap : row)
            if (cap.label == label) return &cap;
    return nullptr;
}

} /* namespace */

TEST_SUITE("KeyboardLayout") {

TEST_CASE("loads the shipped MS7004 layout file")
{
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromFile(ASSETS_DIR "/keyboard/ms7004_layout.txt"));
    REQUIRE(layout.loaded());
    /* Six visual rows: fn strip + four typewriter rows + bottom (space). */
    CHECK(layout.rows().size() >= 5);
}

TEST_CASE("modifier caps are sticky, toggle caps are toggle")
{
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromFile(ASSETS_DIR "/keyboard/ms7004_layout.txt"));

    const auto *shiftCap = findByLabel(layout, "\xd0\x92\xd0\xa0");      /* ВР */
    REQUIRE(shiftCap != nullptr);
    CHECK(shiftCap->key    == ms0515::Key::ShiftL);
    CHECK(shiftCap->sticky == true);
    CHECK(shiftCap->toggle == false);

    const auto *capsCap = findByLabel(layout,
        "\xd0\xa4\xd0\x9a\xd0\xa1");                                    /* ФКС */
    REQUIRE(capsCap != nullptr);
    CHECK(capsCap->key    == ms0515::Key::Caps);
    CHECK(capsCap->sticky == false);
    CHECK(capsCap->toggle == true);

    const auto *ruslatCap = findByLabel(layout,
        "\xd0\xa0\xd0\xa3\xd0\xa1\n\xd0\x9b\xd0\x90\xd0\xa2");          /* РУС\nЛАТ */
    REQUIRE(ruslatCap != nullptr);
    CHECK(ruslatCap->key    == ms0515::Key::RusLat);
    CHECK(ruslatCap->toggle == true);
}

TEST_CASE("a wide blank cap binds to Space")
{
    /* Synthesize a minimal layout: section header + a 7-unit blank. */
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromString("[row5]\n7  \n"));
    REQUIRE(layout.rows().size() == 1);
    REQUIRE(layout.rows()[0].size() == 1);
    CHECK(layout.rows()[0][0].key == ms0515::Key::Space);
}

TEST_CASE("second ВР cap on a row maps to ShiftR")
{
    /* Place two ВР caps in the same section.  The first should bind
     * to ShiftL, the second to ShiftR. */
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromString(
        "[row4]\n"
        "1  \xd0\x92\xd0\xa0\n"      /* first ВР */
        "1  Q\n"
        "1  \xd0\x92\xd0\xa0\n"      /* second ВР */
    ));
    REQUIRE(layout.rows().size() == 1);
    const auto &row = layout.rows()[0];
    REQUIRE(row.size() == 3);
    CHECK(row[0].key == ms0515::Key::ShiftL);
    CHECK(row[2].key == ms0515::Key::ShiftR);
}

TEST_CASE("cosmetic gap _ is undrawn, '=' prefix marks dim")
{
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromString(
        "[row1]\n"
        "1  _\n"
        "1  =\xd0\x9f\xd0\xa4" "1\n"        /* =ПФ1 → drawn but dim */
    ));
    REQUIRE(layout.rows().size() == 1);
    const auto &row = layout.rows()[0];
    REQUIRE(row.size() == 2);
    CHECK(row[0].drawn == false);
    CHECK(row[1].drawn == true);
    CHECK(row[1].dim   == true);
}

TEST_CASE("loadFromFile on a missing path returns false and clears state")
{
    ms0515::KeyboardLayout layout;
    REQUIRE(layout.loadFromString("[row1]\n1  Q\n"));
    REQUIRE(layout.loaded());
    CHECK_FALSE(layout.loadFromFile("/nonexistent/path/to/layout.txt"));
    CHECK_FALSE(layout.loaded());
}

TEST_CASE("isLetterKey: pure letters always letters, symbol-on-letter mode-dep")
{
    using ms0515::Key;
    /* Pure Latin-only letters: same in both modes. */
    CHECK(ms0515::isLetterKey(Key::A, /*rusMode=*/false));
    CHECK(ms0515::isLetterKey(Key::A, /*rusMode=*/true));
    CHECK(ms0515::isLetterKey(Key::Z, /*rusMode=*/false));

    /* Symbol-on-letter caps (Ш/[ etc.): letters only in РУС. */
    CHECK_FALSE(ms0515::isLetterKey(Key::LBracket, /*rusMode=*/false));
    CHECK(      ms0515::isLetterKey(Key::LBracket, /*rusMode=*/true));
    CHECK_FALSE(ms0515::isLetterKey(Key::HardSign, /*rusMode=*/false));
    CHECK(      ms0515::isLetterKey(Key::HardSign, /*rusMode=*/true));

    /* Non-letters: never letters. */
    CHECK_FALSE(ms0515::isLetterKey(Key::Digit1, /*rusMode=*/false));
    CHECK_FALSE(ms0515::isLetterKey(Key::Space,  /*rusMode=*/true));
    CHECK_FALSE(ms0515::isLetterKey(Key::F1,     /*rusMode=*/true));
}

TEST_CASE("isShiftImmuneSymbol: only the four ЛАТ symbol-on-letter caps")
{
    using ms0515::Key;
    /* In ЛАТ, the symbol-on-letter caps would otherwise produce the
     * shifted glyph (`{|}~`) — OSK suppresses Shift instead. */
    CHECK(ms0515::isShiftImmuneSymbol(Key::LBracket,  /*rusMode=*/false));
    CHECK(ms0515::isShiftImmuneSymbol(Key::RBracket,  /*rusMode=*/false));
    CHECK(ms0515::isShiftImmuneSymbol(Key::Backslash, /*rusMode=*/false));
    CHECK(ms0515::isShiftImmuneSymbol(Key::Che,       /*rusMode=*/false));

    /* In РУС they are letters and respond to Shift normally. */
    CHECK_FALSE(ms0515::isShiftImmuneSymbol(Key::LBracket,  /*rusMode=*/true));
    CHECK_FALSE(ms0515::isShiftImmuneSymbol(Key::Backslash, /*rusMode=*/true));

    /* Anything else is not immune in either mode. */
    CHECK_FALSE(ms0515::isShiftImmuneSymbol(Key::A,      /*rusMode=*/false));
    CHECK_FALSE(ms0515::isShiftImmuneSymbol(Key::Digit1, /*rusMode=*/false));
}

} /* TEST_SUITE */
