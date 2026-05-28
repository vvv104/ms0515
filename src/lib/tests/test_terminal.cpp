/*
 * test_terminal.cpp — Terminal mirror diff classification.
 *
 * Builds Snapshot objects by hand (no emulator, no VRAM, no font
 * lookup) and feeds them to Terminal one at a time, capturing the
 * emitted bytes through a tmpfile so we can compare against the
 * exact expected wire output.  These tests pin down the four cases
 * the classifier has to get right: initial dump, append, scroll-up,
 * and full redraw.  Cursor stripping is checked as a side effect of
 * the append test because the OS draws a '_' that toggles on/off
 * between samples.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
}

#include <ms0515/Emulator.hpp>
#include "EmulatorInternal.hpp"
#include <ms0515/Terminal.hpp>

#include "test_disk.hpp"

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <ostream>
#include <string>

using ms0515::Terminal;

namespace {

/* Build a Snapshot whose rows are taken from `rows` (any size up to
 * kRows).  Trailing rows are left blank.  Each input string is
 * copied verbatim into the cell grid; characters past the row are
 * left as 0x20.  Mode is hires by default — the diff cares about
 * cell content, not column count, except where the mode itself
 * differs (which we test explicitly). */
[[nodiscard]]
Terminal::Snapshot makeSnapshot(std::initializer_list<std::string> rows,
                                    int cols = Terminal::kHiresCols)
{
    Terminal::Snapshot s;
    s.cols = cols;
    s.cells.fill(0x20);
    int r = 0;
    for (const auto &row : rows) {
        if (r >= Terminal::kRows) break;
        for (size_t c = 0; c < row.size() &&
                           c < static_cast<size_t>(Terminal::kHiresCols); ++c) {
            s.cells[r * Terminal::kHiresCols + c] =
                static_cast<uint8_t>(row[c]);
        }
        ++r;
    }
    return s;
}

/* RAII tmpfile that lets us read back what Terminal wrote.  Each
 * drain() returns only the bytes appended since the previous drain
 * (we track the file position rather than truncating, which avoids
 * platform-specific calls). */
struct CaptureSink {
    FILE *f = std::tmpfile();
    long  lastDrained = 0;
    ~CaptureSink() { if (f) std::fclose(f); }

    [[nodiscard]] std::string drain()
    {
        std::fflush(f);
        const long endPos = std::ftell(f);
        const long n      = endPos > lastDrained ? endPos - lastDrained : 0;
        std::fseek(f, lastDrained, SEEK_SET);
        std::string out(static_cast<size_t>(n), '\0');
        if (n > 0) {
            const size_t got = std::fread(out.data(), 1,
                                          static_cast<size_t>(n), f);
            out.resize(got);    /* defensive — handles a short read */
        }
        std::fseek(f, endPos, SEEK_SET);
        lastDrained = endPos;
        return out;
    }
};

} /* namespace */

TEST_SUITE("Terminal") {

TEST_CASE("First snapshot preserves the OS row spacing") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({
        "line one",
        "line two",
        "",
        "line four",
    }));

    /* Blank rows BETWEEN content rows reach scrollback as `\n`-only
     * lines so the visual layout the OS drew matches what the user
     * sees.  Leading and trailing blanks (none here, but see the
     * dedicated test below) are dropped; no trailing newline either —
     * lastEmitRow_ tracks the row of the last emitted character so
     * suffix appends continue from the right place. */
    CHECK(sink.drain() == "line one\nline two\n\nline four");
}

TEST_CASE("Initial dump skips leading and trailing blank rows") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Content surrounded by leading/trailing blanks: only the
     * inter-row gap survives — no padding above or below. */
    term.update(makeSnapshot({
        "",
        "",
        "header",
        "",
        "body",
        "",
        "",
    }));

    CHECK(sink.drain() == "header\n\nbody");
}

TEST_CASE("Identical snapshot emits nothing") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    auto s = makeSnapshot({"hello"});
    term.update(s);
    (void)sink.drain();   /* discard initial dump */

    term.update(s);
    CHECK(sink.drain().empty());
}

TEST_CASE("Single-row append emits just the new suffix") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({". DI"}));
    (void)sink.drain();

    term.update(makeSnapshot({". DIR"}));
    CHECK(sink.drain() == "R");

    term.update(makeSnapshot({". DIRECTORY"}));
    CHECK(sink.drain() == "ECTORY");
}

TEST_CASE("Cursor-only flicker (`_` toggling) emits nothing") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Row content `.DIR_` with a blinking cursor — sample after sample
     * the trailing `_` toggles between drawn and erased.  Our trimmer
     * treats the cursor character as a blank, so neither phase counts
     * as a content change. */
    term.update(makeSnapshot({".DIR_"}));
    (void)sink.drain();

    term.update(makeSnapshot({".DIR "}));
    CHECK(sink.drain().empty());

    term.update(makeSnapshot({".DIR_"}));
    CHECK(sink.drain().empty());
}

TEST_CASE("New row starting fresh prefixes the output with `\\n`") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({". DIR"}));
    (void)sink.drain();

    /* OS finished printing on row 0 and started row 1 with output. */
    term.update(makeSnapshot({". DIR", "FOO.DAT"}));
    CHECK(sink.drain() == "\nFOO.DAT");
}

TEST_CASE("Diff path preserves blank rows between two content rows") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Common DIR-listing pattern: "Свободных блоков: N" on row 0,
     * blank row 1, "." prompt on row 2.  When the prompt appears,
     * the diff plan walks rows 0-2 and finds row 0 unchanged, row 1
     * unchanged (blank), row 2 freshly populated.  The forward-jump
     * branch emits (r - cursorRow) `\n`s — two of them — which
     * preserves the blank line as scrollback expects. */
    term.update(makeSnapshot({"Free blocks: 247"}));
    (void)sink.drain();

    term.update(makeSnapshot({"Free blocks: 247", "", "."}));
    CHECK(sink.drain() == "\n\n.");
}

TEST_CASE("Multiple empty Enters at a `.` prompt keep blank rows between dots") {
    /* OSA's monitor responds to Enter by echoing `\n` for the input
     * itself, then printing `\n.` for the next prompt — three lines
     * of state shifts, two of which are blank.  Each Enter therefore
     * advances the cursor by 2 rows on screen, leaving an empty row
     * between consecutive `.` prompts.  The mirror has to reproduce
     * that spacing or `\n.\n.\n.` collapses into a tight column of
     * dots that doesn't match the OS layout. */
    Terminal term;

    /* Initial state: prompt at row 14, everything else blank. */
    auto rowsAt = [](std::initializer_list<std::pair<int, std::string>> at) {
        Terminal::Snapshot s;
        s.cols = Terminal::kHiresCols;
        s.cells.fill(0x20);
        for (const auto &[r, t] : at)
            for (size_t c = 0; c < t.size(); ++c)
                s.cells[r * Terminal::kHiresCols + c] =
                    static_cast<uint8_t>(t[c]);
        return s;
    };

    term.update(rowsAt({{14, "."}}));
    REQUIRE(term.history() == ".");

    /* First empty Enter: dot moved to row 16, row 15 blank. */
    term.update(rowsAt({{14, "."}, {16, "."}}));
    CHECK(term.history() == ".\n\n.");

    /* Second empty Enter: dot moved to row 18, row 17 blank. */
    term.update(rowsAt({{14, "."}, {16, "."}, {18, "."}}));
    CHECK(term.history() == ".\n\n.\n\n.");

    /* Third empty Enter: dot at row 20.  Each cycle should add `\n\n.`,
     * not `\n.` — the bug the user reported was the cursor advancing
     * by one row only, collapsing every gap after the first. */
    term.update(rowsAt({{14, "."}, {16, "."}, {18, "."}, {20, "."}}));
    CHECK(term.history() == ".\n\n.\n\n.\n\n.");
}

TEST_CASE("Scroll-up by one emits a single newline plus the new bottom row") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Fill 25 rows with distinct content. */
    std::initializer_list<std::string> rows = {
        "row00","row01","row02","row03","row04","row05","row06","row07",
        "row08","row09","row10","row11","row12","row13","row14","row15",
        "row16","row17","row18","row19","row20","row21","row22","row23",
        "row24",
    };
    term.update(makeSnapshot(rows));
    (void)sink.drain();

    /* Scroll up by 1: each row picks up its successor's content;
     * a brand-new row "row25" appears at the bottom. */
    term.update(makeSnapshot({
        "row01","row02","row03","row04","row05","row06","row07","row08",
        "row09","row10","row11","row12","row13","row14","row15","row16",
        "row17","row18","row19","row20","row21","row22","row23","row24",
        "row25",
    }));
    CHECK(sink.drain() == "\nrow25");
}

TEST_CASE("Incremental emit converts KOI-8 Cyrillic cells to valid UTF-8") {
    /* Regression for the U+FFFD (`�`) artifacts the user saw: when
     * a row of cells contains KOI-8R Cyrillic codes (0xC0..0xFF),
     * tryEmitIncremental used to push raw single bytes into the
     * history buffer, which is invalid UTF-8 and triggers
     * replacement-character rendering downstream.  Each KOI-8 code
     * must instead become its multi-byte UTF-8 sequence. */
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Initial dump on row 0 (`A`); shadow has only that row. */
    auto initial = makeSnapshot({"A"});
    term.update(initial);
    (void)sink.drain();

    /* Append KOI-8R 'А' (0xE1) then 'Б' (0xE2) on the same row. */
    auto next = makeSnapshot({{ 'A',
                                 static_cast<char>(0xE1),
                                 static_cast<char>(0xE2) }});
    term.update(next);
    /* Expected wire bytes: D0 90 ('А') D0 91 ('Б') — proper UTF-8. */
    const auto out = sink.drain();
    REQUIRE(out.size() == 4);
    CHECK(static_cast<uint8_t>(out[0]) == 0xD0);
    CHECK(static_cast<uint8_t>(out[1]) == 0x90);
    CHECK(static_cast<uint8_t>(out[2]) == 0xD0);
    CHECK(static_cast<uint8_t>(out[3]) == 0x91);
}

TEST_CASE("Multi-row append below the host cursor uses incremental, not redraw") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* OS prompt with a single line of context. */
    term.update(makeSnapshot({". DIR"}));
    (void)sink.drain();

    /* Between samples the OS printed three more rows below the
     * prompt — appearing all at once because we sample every ~10
     * frames, not every frame.  All three rows were blank in the
     * shadow, so the incremental plan emits a `\n` advance for each
     * fresh row.  No redraw marker. */
    term.update(makeSnapshot({
        ". DIR",
        "FOO.SAV  10  blocks",
        "BAR.DAT  56  blocks",
        "BAZ.MAC  3   blocks",
    }));
    const auto out = sink.drain();
    CHECK(out.find("[--- screen redrawn ---]") == std::string::npos);
    CHECK(out == "\nFOO.SAV  10  blocks"
                 "\nBAR.DAT  56  blocks"
                 "\nBAZ.MAC  3   blocks");
}

TEST_CASE("Scroll past kMaxScroll old limit (k=12) is now detected") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    /* Fill 25 rows, then scroll by 12 in a single sample — bursty
     * output (DIR, multi-line print) can do this when the sample
     * cadence is slower than the OS print rate.  Old code capped at
     * k=8 and fell back to redraw; new code accepts up to k=24 as
     * long as the preserved overlap has real content. */
    std::initializer_list<std::string> initial = {
        "row00","row01","row02","row03","row04","row05","row06","row07",
        "row08","row09","row10","row11","row12","row13","row14","row15",
        "row16","row17","row18","row19","row20","row21","row22","row23",
        "row24",
    };
    term.update(makeSnapshot(initial));
    (void)sink.drain();

    term.update(makeSnapshot({
        "row12","row13","row14","row15","row16","row17","row18","row19",
        "row20","row21","row22","row23","row24","new00","new01","new02",
        "new03","new04","new05","new06","new07","new08","new09","new10",
        "new11",
    }));
    const auto out = sink.drain();
    CHECK(out.find("[--- screen redrawn ---]") == std::string::npos);
    /* 12 newlines, each followed by one of the twelve new bottom
     * rows in order. */
    CHECK(out == "\nnew00\nnew01\nnew02\nnew03\nnew04\nnew05"
                 "\nnew06\nnew07\nnew08\nnew09\nnew10\nnew11");
}

TEST_CASE("Scroll-up by k emits k newlines plus the k new bottom rows") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    std::initializer_list<std::string> initial = {
        "A0","A1","A2","A3","A4","A5","A6","A7","A8","A9",
        "B0","B1","B2","B3","B4","B5","B6","B7","B8","B9",
        "C0","C1","C2","C3","C4",
    };
    term.update(makeSnapshot(initial));
    (void)sink.drain();

    /* Scroll up by 3 — top three lost, three new at bottom. */
    term.update(makeSnapshot({
        "A3","A4","A5","A6","A7","A8","A9","B0","B1","B2",
        "B3","B4","B5","B6","B7","B8","B9","C0","C1","C2",
        "C3","C4","D0","D1","D2",
    }));
    CHECK(sink.drain() == "\nD0\nD1\nD2");
}

TEST_CASE("Wholly different multi-row content reprints as a screen redraw") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({
        "first line",
        "second line",
    }));
    (void)sink.drain();

    /* Every visible row is new and there are enough of them (≥3) to
     * justify a screen-redraw event: the OS cleared the screen and
     * reprinted from scratch.  Emit a blank-line separator from the
     * previous on-wire state and reprint the screen, with
     * lastScreenStart() pointing at the new content so the UI can
     * scroll it to viewport top. */
    term.update(makeSnapshot({
        "completely",
        "different",
        "screen now",
    }));
    const auto out = sink.drain();
    CHECK(out ==
          "\n"
          "-------------------------------- screen redrawn "
          "--------------------------------\n"
          "completely\ndifferent\nscreen now");
}

TEST_CASE("Single-row all-new content appends instead of redrawing") {
    /* Real-system case: after "НГМД готов..." the OS clears the
     * screen and prints just "© 1992 Родионов С.А." on row 2 — only
     * one non-blank row of new content.  Treating that as a redraw
     * would yank the viewport past "НГМД готов..." and the user
     * would lose the boot prompt.  Below the redraw threshold (3+
     * rows) we fall through to linear dedup: emit just the new line
     * with a leading \n, no \n\n separator, and don't move
     * lastScreenStart() — the UI keeps the previous anchor. */
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({"НГМД готов..."}));
    const std::size_t startBefore = term.lastScreenStart();
    (void)sink.drain();

    term.update(makeSnapshot({
        "",
        "",
        "        @ 1992 author",
    }));
    /* What matters: lastScreenStart() didn't advance — the UI keeps
     * its previous anchor.  tryEmitIncremental may legitimately emit
     * \n\n as part of a forward-row jump, so we don't pin the byte
     * sequence; the anchor is the part that affects scroll behavior. */
    CHECK(term.lastScreenStart() == startBefore);
}

TEST_CASE("Re-layout: new line above preserved content reprints whole screen") {
    /* The user's POST-screen scenario: the OS first draws results
     * sparsely (with blank rows between entries), then redraws them
     * compactly with a brand-new heading *above* preserved entries.
     * Linear `\n`-only output cannot insert a new line above content
     * already on the wire, so we reprint the current screen with a
     * blank-line separator — scrollback then shows the layout the
     * way the OS sees it. */
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({
        "",
        "    Test ABC",
        "",
        "    Test XYZ",
        "",
        "    Done",
    }));
    (void)sink.drain();

    term.update(makeSnapshot({
        "    Header line",
        "    Test ABC",
        "    Test XYZ",
        "    Done",
    }));
    const auto out = sink.drain();
    CHECK(out ==
          "\n"
          "-------------------------------- screen redrawn "
          "--------------------------------\n"
          "    Header line\n    Test ABC\n    Test XYZ\n    Done");
}

TEST_CASE("Scroll plus suffix-extension on the boundary row emits clean tail") {
    /* The user's Mihin scroll: the bottom row of the preserved
     * overlap was being printed when the previous sample fired
     * ("^T Auto RUN D") and is now complete ("^T Auto RUN DBAS ALL").
     * detectScrollUp's strict equality misses it.  emitDedup
     * recognises that the new line starts with the last one we
     * emitted and writes only the appended tail; the new bottom
     * rows then follow on fresh lines. */
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({
        "header line",
        "DZ V1.00",
        "SL V8.00",
        "^T   Auto   RUN D",
    }));
    (void)sink.drain();

    term.update(makeSnapshot({
        "DZ V1.00",
        "SL V8.00",
        "^T   Auto   RUN DBAS ALL",
        "----------",
        "Free..>161.",
        ".",
    }));
    const auto out = sink.drain();
    CHECK(out.find("[--- screen redrawn ---]") == std::string::npos);
    CHECK(out == "BAS ALL\n----------\nFree..>161.\n.");
}

TEST_CASE("Backspace shrinks the current line in place") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({"HELLO"}));
    (void)sink.drain();

    /* User pressed backspace twice; row trimmed back to "HEL".
     * Two cells erased — host FILE* gets `\b \b` per cell, which a
     * real terminal interprets as cursor-back / blank / cursor-back
     * (proper destructive backspace). */
    term.update(makeSnapshot({"HEL"}));
    CHECK(sink.drain() == "\b \b\b \b");
}

TEST_CASE("Backspace also shrinks the in-memory history buffer") {
    /* The ImGui scrollback view reads `term.history()` directly and
     * doesn't interpret `\b`, so the history buffer has to be
     * truncated outright — emitting raw `\b` bytes there would leave
     * a literal control character in the rendered text.  Drive a
     * stepwise shrink and check the buffer length each step. */
    Terminal term;
    term.update(makeSnapshot({"HELLO"}));
    REQUIRE(term.history() == "HELLO");

    term.update(makeSnapshot({"HEL"}));
    CHECK(term.history() == "HEL");

    term.update(makeSnapshot({"H"}));
    CHECK(term.history() == "H");
}

TEST_CASE("Same-row prefix divergence backspaces over the changed suffix") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({"DIR FOO.BAR"}));
    (void)sink.drain();

    /* "DIR FOO.BAR" → "DEL FOO": the common prefix is just "D".  We
     * back over the 10 trailing oldText cells, then write the 6
     * trailing newText cells.  History also shrinks accordingly so
     * the in-app scrollback shows "DEL FOO". */
    term.update(makeSnapshot({"DEL FOO"}));
    CHECK(sink.drain() ==
          /* 10 × `\b \b` */ "\b \b\b \b\b \b\b \b\b \b"
                              "\b \b\b \b\b \b\b \b\b \b"
          /* 6 × content   */ "EL FOO");
    CHECK(term.history() == "DEL FOO");
}

TEST_CASE("feedSample lets a ROM-monitor `@` prompt column through") {
    /* MS-0515 ROM monitor prints `@` for each input cycle.  Pressing
     * Enter alone advances cursor and prints `\n@` for the new
     * prompt — the screen ends up with a column of one-character
     * `@` rows.  Pre-fix, the no-adjacent-duplicate gate would
     * flag two consecutive `@` rows as a mid-scroll-copy and drop
     * every snap; now the gate skips rows with fewer than 3
     * non-blank glyphs (mid-scroll-copy false positives are about
     * file/path content, not single-character prompts). */
    Terminal term;

    auto rowsAt = [](std::initializer_list<std::pair<int, std::string>> at) {
        Terminal::Snapshot s;
        s.cols = Terminal::kHiresCols;
        s.cells.fill(0x20);
        for (const auto &[r, t] : at)
            for (size_t c = 0; c < t.size(); ++c)
                s.cells[r * Terminal::kHiresCols + c] =
                    static_cast<uint8_t>(t[c]);
        return s;
    };

    term.feedSample(rowsAt({{20, "@"}}));
    REQUIRE(term.history() == "@");

    term.feedSample(rowsAt({{20, "@"}, {21, "@"}}));
    CHECK(term.history() == "@\n@");

    term.feedSample(rowsAt({{20, "@"}, {21, "@"}, {22, "@"}}));
    CHECK(term.history() == "@\n@\n@");

    term.feedSample(rowsAt({{20, "@"}, {21, "@"}, {22, "@"}, {23, "@"}}));
    CHECK(term.history() == "@\n@\n@\n@");
}

TEST_CASE("feedSample lets a column of identical prompts through") {
    /* Empty-Enter at a `.` prompt makes OSA print just `\n.` — the
     * new prompt lands on the row right below the previous one, so
     * after a couple of empty Enters the screen has two or more
     * consecutive `.` rows.  The no-adjacent-duplicate gate used
     * to flag that as a mid-scroll-copy artifact and reject every
     * snap, so subsequent prompts piled up un-emitted until enough
     * scrolling broke the streak.  Now the gate cross-checks
     * against the previous accepted sample — only true mid-scroll
     * copies (where the duplicated content was already at row R+1
     * in shadow) are blocked. */
    Terminal term;

    auto rowsAt = [](std::initializer_list<std::pair<int, std::string>> at) {
        Terminal::Snapshot s;
        s.cols = Terminal::kHiresCols;
        s.cells.fill(0x20);
        for (const auto &[r, t] : at)
            for (size_t c = 0; c < t.size(); ++c)
                s.cells[r * Terminal::kHiresCols + c] =
                    static_cast<uint8_t>(t[c]);
        return s;
    };

    /* First prompt at row 14, then three empty Enters land prompts
     * at rows 15, 16, 17 (consecutive lines, no gaps). */
    term.feedSample(rowsAt({{14, "."}}));
    REQUIRE(term.history() == ".");

    term.feedSample(rowsAt({{14, "."}, {15, "."}}));
    CHECK(term.history() == ".\n.");

    term.feedSample(rowsAt({{14, "."}, {15, "."}, {16, "."}}));
    CHECK(term.history() == ".\n.\n.");

    term.feedSample(rowsAt({{14, "."}, {15, "."}, {16, "."}, {17, "."}}));
    CHECK(term.history() == ".\n.\n.\n.");
}

TEST_CASE("feedSample still rejects a true mid-scroll-copy") {
    /* Original mid-scroll signature must keep getting blocked: the
     * OS copies row R+1 down onto row R cell-by-cell, leaving both
     * rows holding the row-R+1 content for a few sample cycles.
     * Pre-fix, that was the canonical "MORDA .SCR / MORDA .SCR"
     * duplication gate.  Post-fix, the cross-shadow check still
     * catches it — `shadow.row[R+1]` was the same content, so we
     * recognise the dup as a transient and drop the snap. */
    Terminal term;

    auto rowsAt = [](std::initializer_list<std::pair<int, std::string>> at) {
        Terminal::Snapshot s;
        s.cols = Terminal::kHiresCols;
        s.cells.fill(0x20);
        for (const auto &[r, t] : at)
            for (size_t c = 0; c < t.size(); ++c)
                s.cells[r * Terminal::kHiresCols + c] =
                    static_cast<uint8_t>(t[c]);
        return s;
    };

    /* Establish lastForwardedSnap_ with two rows: row 23 = "OLD",
     * row 24 = "NEW" (where "NEW" is the content scroll is bringing
     * down). */
    term.feedSample(rowsAt({{23, "OLD"}, {24, "NEW"}}));
    REQUIRE(!term.history().empty());
    const auto baseline = term.history();

    /* Mid-scroll-copy: cur.row[23] = "NEW" (copied), cur.row[24] =
     * "NEW" still (not yet cleared).  shadow.row[24] = "NEW" too,
     * so the gate's cross-check fires and the snap is dropped. */
    term.feedSample(rowsAt({{23, "NEW"}, {24, "NEW"}}));
    CHECK(term.history() == baseline);
}

TEST_CASE("feedSample lets a same-row shrink through the progressing gate") {
    /* Live samplng goes through `feedSample`, which runs the
     * stability gates before forwarding to update().  The progressing
     * gate used to flag `cur.row[R]` as a "strict-subset partial
     * copy" of `lastForwardedSnap_.row[R]` whenever the row got
     * shorter — meaning every single-cell backspace was dropped
     * until the row fell under the 3-non-blank noise floor.  Now
     * same-row shrinkage is recognised as legitimate progress
     * (backspace, DEL, end-of-line clear), and the gate only fires
     * on cross-row matches (mid-scroll-copy of row R+1 down onto
     * row R). */
    Terminal term;

    /* Establish lastForwardedSnap_ with a 4-cell row. */
    term.feedSample(makeSnapshot({"ABCD"}));
    REQUIRE(term.history() == "ABCD");

    /* Single-cell backspace, well above the curNonBlank<3 floor. */
    term.feedSample(makeSnapshot({"ABC"}));
    CHECK(term.history() == "ABC");

    term.feedSample(makeSnapshot({"AB"}));
    CHECK(term.history() == "AB");
}

TEST_CASE("Backspace pops the right UTF-8 byte count for KOI-8 Cyrillic") {
    /* KOI-8R 0xE1 ('А') decodes to two UTF-8 bytes (D0 90).  When
     * the user erases that cell, the history buffer must shed both
     * bytes — popping just one would leave a half-character that
     * ImGui renders as U+FFFD. */
    Terminal term;
    auto initial = makeSnapshot({{ 'A',
                                    static_cast<char>(0xE1),
                                    static_cast<char>(0xE2) }});
    term.update(initial);
    REQUIRE(term.history().size() == 5);   /* "A" + 2+2 UTF-8 bytes */

    term.update(makeSnapshot({{ 'A',
                                 static_cast<char>(0xE1) }}));
    /* One Cyrillic cell erased → 2 UTF-8 bytes popped. */
    CHECK(term.history().size() == 3);

    term.update(makeSnapshot({{ 'A' }}));
    CHECK(term.history() == "A");
}

TEST_CASE("reset() makes the next update emit as initial again") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({"first"}));
    (void)sink.drain();

    term.reset();
    term.update(makeSnapshot({"second"}));
    /* No diff against the wiped shadow — initial dump path again. */
    CHECK(sink.drain() == "second");
}

TEST_CASE("Null output is a safe no-op for FILE* but history still fills") {
    Terminal term;     /* out_ stays nullptr */
    term.update(makeSnapshot({"anything"}));
    term.update(makeSnapshot({"anything", "more"}));
    /* History buffer captures the same bytes that would have gone
     * to a FILE*, so a UI window opened later can render full
     * scrollback even if no host stream was ever attached. */
    CHECK(term.history() == "anything\nmore");
}

TEST_CASE("history() matches the FILE* stream byte-for-byte") {
    CaptureSink sink;
    Terminal term;
    term.setOutput(sink.f);

    term.update(makeSnapshot({"abc"}));
    term.update(makeSnapshot({"abcd"}));
    term.update(makeSnapshot({"abcd", "next"}));

    /* The sink got everything, the history mirrors it. */
    const auto piped     = sink.drain();
    const auto in_memory = std::string(term.history());
    CHECK(piped == in_memory);
}

TEST_CASE("clearHistory() empties the buffer without disturbing the shadow") {
    Terminal term;
    term.update(makeSnapshot({"old"}));
    REQUIRE(!term.history().empty());

    term.clearHistory();
    CHECK(term.history().empty());

    /* Shadow is intact — diff against last update still works as
     * "no change", so update() emits nothing. */
    term.update(makeSnapshot({"old"}));
    CHECK(term.history().empty());

    /* But a real diff is still picked up against the preserved shadow. */
    term.update(makeSnapshot({"old", "new"}));
    CHECK(term.history() == "\nnew");
}

/* ── Real-system integration ─────────────────────────────────────────── */

/* Walk every cell of a snapshot and dump the raw 8-byte VRAM bitmaps
 * for cells that decoded to kUnknownGlyph.  Used by the diagnostic
 * tests below so we can see what bitmaps actually fail glyph lookup
 * during a real boot. */
static void dumpUnknownCells(const Terminal::Snapshot &snap,
                             const uint8_t *vram, bool hires, int maxRows)
{
    const int colStride = hires ? 1 : 2;
    int dumped = 0;
    for (int r = 0; r < Terminal::kRows && dumped < maxRows; ++r) {
        for (int c = 0; c < snap.cols && dumped < maxRows; ++c) {
            const uint8_t code = snap.cells[r * Terminal::kHiresCols + c];
            if (code != Terminal::kUnknownGlyph) continue;
            std::fprintf(stderr, "[diag] r=%2d c=%2d  bytes=", r, c);
            uint64_t key = 0;
            for (int y = 0; y < 8; ++y) {
                const int off = (r * 8 + y) * 80 + c * colStride;
                std::fprintf(stderr, "%02x ", vram[off]);
                key |= static_cast<uint64_t>(vram[off]) << (y * 8);
            }
            std::fprintf(stderr, " popcount=%d\n", std::popcount(key));
            ++dumped;
        }
    }
}

/* Print every non-blank row of the snapshot so we can see what the
 * screen reader actually decoded — this is what the user sees on the
 * framebuffer (modulo cursor blink, which is irrelevant here because
 * we sample only at quiescent moments). */
static void dumpSnapshotRows(const char *label,
                             const Terminal::Snapshot &snap)
{
    std::fprintf(stderr, "[diag] %s screen:\n", label);
    for (int r = 0; r < Terminal::kRows; ++r) {
        const auto rt = snap.row(r);
        if (rt.empty()) continue;
        std::fprintf(stderr, "[diag]   %2d: %.*s\n", r,
                     static_cast<int>(rt.size()), rt.data());
    }
}

TEST_CASE("DIAG: Rodionov OSA-B real boot — screen vs Terminal mirror"
          * doctest::skip())
{
    /* User-facing scenario: ROM-B + Rodionov 1992 OSA, sampled the
     * way the live App samples (10-frame interval + quiescence gate).
     * Goal of this test: SHOW the final framebuffer text and the
     * Terminal's history side-by-side so we can pinpoint exactly
     * which cells the user is seeing as stray █ in scrollback.  Also
     * dumps the raw VRAM bitmap of every unknown-glyph cell so the
     * decision logic in Terminal::lookup() can be tuned against
     * real data instead of guesswork. */
    namespace fs = std::filesystem;
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    ms0515_test::TempDisk td{std::string{ASSETS_DIR} + "/disks/rodionov.dsk"};
    REQUIRE(emu.mountDisk(0, td.path().string()));
    std::error_code ec;
    if (auto sz = fs::file_size(td.path(), ec); !ec && sz == 2 * 409600u)
        (void)emu.mountDisk(2, td.path().string());
    emu.enableRamDisk();
    emu.reset();

    Terminal term;

    /* Drive each tick's raw sample through Terminal::feedSample —
     * Terminal owns the stability gates; the test just supplies
     * the stream. */
    auto sample = [&]{ term.feedSample(term.decode(emu)); };

    auto checkpoint = [&](const char *label){
        auto snap = term.decode(emu);
        std::fprintf(stderr, "[diag] === %s ===\n", label);
        dumpSnapshotRows(label, snap);
        dumpUnknownCells(snap, ms0515::internal::vram(emu).data(), emu.isHires(), /*maxRows=*/40);
    };

    /* Boot in chunks so we can checkpoint along the way and see
     * which moments leak unknown-glyph cells into scrollback. */
    constexpr int kChunks[]   = {30, 60, 100, 200, 500, 1500, 2500};
    int           lastFrame   = 0;
    for (int target : kChunks) {
        while (lastFrame < target) {
            (void)emu.stepFrame();
            sample();
            ++lastFrame;
        }
        checkpoint((std::string{"frame "} + std::to_string(target)).c_str());
    }

    std::fprintf(stderr, "[diag] Terminal history (%zu bytes):\n",
                 term.history().size());
    /* Print history with a leader on each line so the boundary
     * between scrollback content and shell-tagged output is easy to
     * spot in the test runner output. */
    const std::string leader = "[diag]   | ";
    std::fputs(leader.c_str(), stderr);
    for (char c : term.history()) {
        std::fputc(c, stderr);
        if (c == '\n') std::fputs(leader.c_str(), stderr);
    }
    std::fputc('\n', stderr);

    CHECK(true);
}

} /* TEST_SUITE */
