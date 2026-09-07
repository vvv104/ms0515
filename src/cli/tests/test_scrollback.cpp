/*
 * test_scrollback.cpp — the rows that leave the machine's screen at the
 * top, kept: a clean scroll, a scroll caught half-way by the frame, a
 * row caught half-copied, a blank bottom, a redraw, and no duplicates.
 */
#include "Scrollback.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ms0515;

namespace {

using Snap = VramMirror::Snapshot;

Snap blank()
{
    Snap s;
    s.cells.fill(0x20);
    return s;
}

void put(Snap &s, int row, const std::string &text)
{
    for (int col = 0; col < VramMirror::kCols; ++col)
        s.cells[static_cast<size_t>(row) * VramMirror::kCols + static_cast<size_t>(col)] =
            col < static_cast<int>(text.size()) ? static_cast<uint8_t>(text[static_cast<size_t>(col)]) : 0x20;
}

/* A screen with `lines` from row 0 down. */
Snap screen(const std::vector<std::string> &lines)
{
    Snap s = blank();
    for (size_t i = 0; i < lines.size() && i < VramMirror::kRows; ++i) put(s, static_cast<int>(i), lines[i]);
    return s;
}

std::vector<std::string> rows(int n, const std::string &prefix)
{
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) out.push_back(prefix + std::to_string(i));
    return out;
}

std::string lineText(const cli::Scrollback &sb, size_t i)
{
    std::string t;
    for (const uint8_t c : sb.lines()[i].cells) t += static_cast<char>(c);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

} // namespace

TEST_CASE("printing in place keeps nothing; a clean scroll keeps the row that left; three at once keep three in order")
{
    cli::Scrollback sb;
    auto full = rows(25, "line ");
    sb.frame(screen(full));
    CHECK(sb.lines().empty());
    /* the last row changes in place: text printed */
    full[24] = "line 24 and more";
    sb.frame(screen(full));
    CHECK(sb.lines().empty());
    /* a scroll by one: rows move up, a new last row */
    auto shifted = std::vector<std::string>(full.begin() + 1, full.end());
    shifted.push_back("line 25");
    sb.frame(screen(shifted));
    REQUIRE(sb.lines().size() == 1);
    CHECK(lineText(sb, 0) == "line 0");
    /* three rows in one frame */
    auto three = std::vector<std::string>(shifted.begin() + 3, shifted.end());
    three.insert(three.end(), {"line 26", "line 27", "line 28"});
    sb.frame(screen(three));
    REQUIRE(sb.lines().size() == 4);
    CHECK(lineText(sb, 1) == "line 1");
    CHECK(lineText(sb, 2) == "line 2");
    CHECK(lineText(sb, 3) == "line 3");
    /* the same frame again changes nothing */
    sb.frame(screen(three));
    CHECK(sb.lines().size() == 4);
}

TEST_CASE("a scroll caught half-way - the top rows moved, the rest not yet - keeps the row once, when the scroll completes")
{
    cli::Scrollback sb;
    const auto full = rows(25, "row ");
    sb.frame(screen(full));
    /* rows 0..9 already copied from below, rows 10..24 still the old ones */
    auto half = full;
    for (int r = 0; r < 10; ++r) half[static_cast<size_t>(r)] = full[static_cast<size_t>(r) + 1];
    sb.frame(screen(half));
    CHECK(sb.lines().empty());
    auto done = std::vector<std::string>(full.begin() + 1, full.end());
    done.push_back("row 25");
    sb.frame(screen(done));
    REQUIRE(sb.lines().size() == 1);
    CHECK(lineText(sb, 0) == "row 0");
    /* and the next clean scroll follows on */
    auto next = std::vector<std::string>(done.begin() + 1, done.end());
    next.push_back("row 26");
    sb.frame(screen(next));
    REQUIRE(sb.lines().size() == 2);
    CHECK(lineText(sb, 1) == "row 1");
}

TEST_CASE("a row caught half-copied - part old, part new - still keeps the row that left, once")
{
    cli::Scrollback sb;
    const auto full = rows(25, "text ");
    sb.frame(screen(full));
    auto torn = full;
    for (int r = 0; r < 12; ++r) torn[static_cast<size_t>(r)] = full[static_cast<size_t>(r) + 1];
    torn[12] = "text 1" + full[12].substr(6);   /* row 12: the first cells from row 13, the rest still its own */
    sb.frame(screen(torn));
    auto done = std::vector<std::string>(full.begin() + 1, full.end());
    done.push_back("text 25");
    sb.frame(screen(done));
    REQUIRE(sb.lines().size() == 1);
    CHECK(lineText(sb, 0) == "text 0");
    auto next = std::vector<std::string>(done.begin() + 1, done.end());
    next.push_back("text 26");
    sb.frame(screen(next));
    REQUIRE(sb.lines().size() == 2);
    CHECK(lineText(sb, 1) == "text 1");
}

TEST_CASE("a screen with a blank bottom scrolls its top row out like any other; blank rows that leave are kept as blank lines")
{
    cli::Scrollback sb;
    sb.frame(screen({"first", "second", "", "fourth"}));
    sb.frame(screen({"second", "", "fourth", "fifth"}));
    REQUIRE(sb.lines().size() == 1);
    CHECK(lineText(sb, 0) == "first");
    sb.frame(screen({"", "fourth", "fifth", "sixth"}));
    sb.frame(screen({"fourth", "fifth", "sixth", "seventh"}));
    REQUIRE(sb.lines().size() == 3);
    CHECK(lineText(sb, 1) == "second");
    CHECK(lineText(sb, 2) == "");
    /* an all-blank screen scrolling is no scroll at all */
    cli::Scrollback empty;
    empty.frame(blank());
    empty.frame(blank());
    CHECK(empty.lines().empty());
}

TEST_CASE("a redraw - the screen cleared, something else drawn - keeps nothing and starts afresh")
{
    cli::Scrollback sb;
    sb.frame(screen(rows(25, "old ")));
    sb.frame(screen({"MENU", "1. one", "2. two"}));
    CHECK(sb.lines().empty());
    sb.frame(screen({"1. one", "2. two", "3. three"}));
    REQUIRE(sb.lines().size() == 1);
    CHECK(lineText(sb, 0) == "MENU");
}

TEST_CASE("the inverted cells travel with the line")
{
    cli::Scrollback sb;
    Snap a = screen({"top", "under"});
    a.inverted[1] = true;
    sb.frame(a);
    sb.frame(screen({"under", "next"}));
    REQUIRE(sb.lines().size() == 1);
    CHECK(sb.lines()[0].inverted[1]);
    CHECK_FALSE(sb.lines()[0].inverted[0]);
}
