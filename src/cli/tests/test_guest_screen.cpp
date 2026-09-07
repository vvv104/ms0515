/*
 * test_guest_screen.cpp — the machine's text screen, as the mirror
 * decoded it, drawn as FTXUI rows: the characters, the inverted cells,
 * the rows asked for.
 */
#include "GuestScreen.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace ms0515;

namespace {

VramMirror::Snapshot blank()
{
    VramMirror::Snapshot s;
    s.cells.fill(0x20);
    return s;
}

void put(VramMirror::Snapshot &s, int row, int col, const std::string &koi8, bool inverted = false)
{
    for (size_t i = 0; i < koi8.size(); ++i) {
        const size_t at = static_cast<size_t>(row) * VramMirror::kCols + static_cast<size_t>(col) + i;
        s.cells[at] = static_cast<uint8_t>(koi8[i]);
        s.inverted[at] = inverted;
    }
}

ftxui::Screen draw(const ftxui::Element &e, int rows)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(VramMirror::kCols), ftxui::Dimension::Fixed(rows));
    ftxui::Render(screen, e);
    return screen;
}

} // namespace

TEST_CASE("the rows asked for come out cell by cell, inverted where the screen is")
{
    auto s = blank();
    put(s, 0, 0, ".DIR");
    put(s, 1, 2, "\xE6\xC1\xCA\xCC", true);   /* KOI-8 'файл' in upper case, inverted */
    put(s, 24, 79, "_");
    const auto top = draw(cli::guestRows(s, 0, 2), 2);
    CHECK(top.PixelAt(0, 0).character == ".");
    CHECK(top.PixelAt(3, 0).character == "R");
    CHECK_FALSE(top.PixelAt(0, 0).inverted);
    CHECK(top.PixelAt(2, 1).character == "\xD0\xA4");   /* Ф */
    CHECK(top.PixelAt(2, 1).inverted);
    CHECK(top.PixelAt(5, 1).inverted);
    CHECK_FALSE(top.PixelAt(6, 1).inverted);
    CHECK(top.PixelAt(6, 1).character == " ");

    const auto bottom = draw(cli::guestRows(s, 23, 25), 2);
    CHECK(bottom.PixelAt(79, 1).character == "_");
    CHECK(bottom.PixelAt(0, 0).character == " ");
    /* no cursor is painted into the rows: the terminal's own sits there */
    const auto plain = draw(cli::guestRows(s, 0, 2), 2);
    CHECK_FALSE(plain.PixelAt(4, 0).inverted);
}

TEST_CASE("the whole screen is 25 rows; a range past the screen is clipped, an empty one is nothing")
{
    auto s = blank();
    put(s, 12, 0, "MIDDLE");
    const auto all = draw(cli::guestRows(s, 0, VramMirror::kRows), VramMirror::kRows);
    CHECK(all.PixelAt(0, 12).character == "M");
    /* an unpainted cell keeps the screen's own blank */
    const auto blankCell = [](const auto &p) { return p.character.empty() || p.character == " "; };
    const auto clipped = draw(cli::guestRows(s, 24, 40), 3);
    CHECK(blankCell(clipped.PixelAt(0, 0)));
    CHECK(blankCell(clipped.PixelAt(0, 1)));
    const auto none = draw(cli::guestRows(s, 5, 5), 1);
    CHECK(blankCell(none.PixelAt(0, 0)));
}

TEST_CASE("with the panels hidden the guest's cursor row lands where it sits under the panels, the key bar kept below")
{
    /* a 25-row terminal: under the panels the guest's cursor row is row 22 */
    auto p = cli::placeGuest(24, 25);
    CHECK(p.pad == 0);
    CHECK(p.from == 2);
    CHECK(p.to == VramMirror::kRows);
    CHECK(p.pad + (24 - p.from) == 22);
    /* the cursor at the top of the guest's screen: padded down to the same row */
    p = cli::placeGuest(0, 25);
    CHECK(p.pad == 22);
    CHECK(p.from == 0);
    CHECK(p.to == 2);
    /* a tall terminal shows the whole screen, padded */
    p = cli::placeGuest(24, 40);
    CHECK(p.pad == 13);
    CHECK(p.from == 0);
    CHECK(p.to == VramMirror::kRows);
    CHECK(p.pad + 24 == 37);
    /* a short one shows what fits above the cursor and one row below */
    p = cli::placeGuest(10, 8);
    CHECK(p.pad == 0);
    CHECK(p.from == 5);
    CHECK(p.to == 12);
}
