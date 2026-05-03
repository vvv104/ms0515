/*
 * Terminal.cpp — Output-only host-terminal mirror.
 *
 * Classification cascade for each delta:
 *
 *   no diff           → emit nothing.
 *   scroll-up by k    → emit `k` newlines, each followed by the
 *                       new-bottom row content.  k may be anything
 *                       up to kRows-1 — bursts of output between
 *                       samples often shift the screen by many lines
 *                       at once.  We require the preserved overlap
 *                       to contain real text so a near-empty screen
 *                       doesn't match itself trivially.
 *   incremental       → row-by-row append.  Each changed row is
 *                       either the host's current line (suffix
 *                       appended in place, or full-line rewrite if
 *                       the prefix changed) or strictly below it
 *                       (`\n`s to advance, then full content).  Any
 *                       backwards change rejects this strategy.
 *   redraw            → marker line + every non-blank row.  Used as
 *                       the fallback when neither scroll nor
 *                       incremental fits.
 *
 * The "host cursor row" is tracked in lastEmitRow_ — initialised to
 * the highest non-empty row of the very first snapshot we dump, then
 * updated by every emit path.  No ANSI cursor positioning: the host
 * terminal's natural top-down flow plus its own scroll buffer give
 * the user back the scrollback the MS-0515 itself does not have.
 */

#include <ms0515/Terminal.hpp>

#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace ms0515 {

Terminal::Terminal()
{
    shadow_.cells.fill(0x20);
    shadow_.cols = ScreenReader::kHiresCols;
}

void Terminal::reset() noexcept
{
    shadow_.cells.fill(0x20);
    shadow_.cols          = ScreenReader::kHiresCols;
    hasShadow_            = false;
    lastEmitRow_          = -1;
    lastEmittedLine_.clear();
    lastScreenStart_      = 0;
    hasLastForwardedSnap_ = false;
}

void Terminal::emitChar(char c)
{
    history_.push_back(c);
    if (out_) std::fputc(static_cast<unsigned char>(c), out_);
}

void Terminal::emitText(std::string_view s)
{
    history_.append(s);
    if (out_ && !s.empty())
        std::fwrite(s.data(), 1, s.size(), out_);
}

void Terminal::emitKoi8(uint8_t koi8)
{
    ScreenReader::appendKoi8Char(history_, koi8);
    if (out_) ScreenReader::putKoi8Char(out_, koi8);
}

std::string Terminal::trimmedRow(const Snapshot &s, int r) const
{
    if (r < 0 || r >= ScreenReader::kRows) return {};
    const int cols = s.cols;
    std::string text;
    text.reserve(static_cast<size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        uint8_t code = s.cells[r * ScreenReader::kHiresCols + c];
        /* Any character in transparentChars_ collapses to a space —
         * cursor blink and unknown-glyph placeholders should not
         * register as content for diff classification. */
        if (transparentChars_.find(static_cast<char>(code)) != std::string::npos)
            code = 0x20;
        text.push_back(static_cast<char>(code));
    }
    while (!text.empty() && static_cast<uint8_t>(text.back()) == 0x20)
        text.pop_back();
    return text;
}

int Terminal::lastNonEmptyRow(const Snapshot &s) const
{
    for (int r = ScreenReader::kRows - 1; r >= 0; --r) {
        if (!trimmedRow(s, r).empty()) return r;
    }
    return -1;
}

int Terminal::detectScrollUp(const Snapshot &cur) const
{
    if (!hasShadow_)              return 0;
    if (shadow_.cols != cur.cols) return 0;

    /* Try every plausible scroll amount.  k=1 is the dominant case
     * but bursts of output (DIR listings, long boot messages) can
     * shift the visible window by many rows in a single 200-ms
     * sample window. */
    for (int k = 1; k < ScreenReader::kRows; ++k) {
        bool ok        = true;
        int  matched   = 0;
        for (int i = 0; i + k < ScreenReader::kRows; ++i) {
            const auto sh = trimmedRow(shadow_, i + k);
            const auto cu = trimmedRow(cur, i);
            if (sh != cu) { ok = false; break; }
            if (!sh.empty()) ++matched;
        }
        /* Reject low-content matches.  With <2 non-blank preserved
         * rows we can't really tell scroll from "different screen
         * that happens to share one row" — the multi-row append /
         * redraw paths handle that case more honestly. */
        if (ok && matched >= 2) return k;
    }
    return 0;
}

bool Terminal::isUnchanged(const Snapshot &cur) const
{
    if (!hasShadow_)              return false;
    if (shadow_.cols != cur.cols) return false;
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        if (trimmedRow(shadow_, r) != trimmedRow(cur, r))
            return false;
    }
    return true;
}

void Terminal::emitRowText(const Snapshot &snap, int row)
{
    const std::string text = trimmedRow(snap, row);
    for (uint8_t c : text)
        emitKoi8(c);
}

void Terminal::emitRowLine(const Snapshot &snap, int row)
{
    emitRowText(snap, row);
    emitChar('\n');
}

bool Terminal::tryEmitIncremental(const Snapshot &cur)
{
    /* Two-pass: build a list of operations first so we can validate
     * the plan before any output is committed; then replay it
     * through the real emit helpers.  The split also routes content
     * bytes through emitKoi8 (which handles KOI-8R → UTF-8) instead
     * of emitChar (raw byte): writing a Cyrillic KOI-8 code as a
     * single byte produced invalid UTF-8 in history_, which is what
     * the user saw rendered as U+FFFD (`�`) in the Terminal window. */
    struct Op {
        enum Kind { Newline, Carriage, Space, Content };
        Kind     kind;
        uint8_t  koi8;   /* used only for Content */
    };

    int cursorRow = lastEmitRow_;
    int cursorCol = cursorRow >= 0
                  ? static_cast<int>(trimmedRow(shadow_, cursorRow).size())
                  : 0;

    std::vector<Op> plan;

    for (int r = 0; r < ScreenReader::kRows; ++r) {
        const std::string oldText = trimmedRow(shadow_, r);
        const std::string newText = trimmedRow(cur, r);
        if (oldText == newText) continue;

        if (r > cursorRow) {
            /* Forward jump.  Advance host cursor with `\n`s, then
             * write the full new row content.  oldText must be empty
             * — this row hasn't been touched before, so we only
             * accept the "fresh row" pattern. */
            if (!oldText.empty()) return false;
            const int nNl = (cursorRow < 0 ? r : r - cursorRow);
            for (int i = 0; i < nNl; ++i)
                plan.push_back({Op::Newline, 0});
            for (uint8_t c : newText)
                plan.push_back({Op::Content, c});
            cursorRow = r;
            cursorCol = static_cast<int>(newText.size());
        } else if (r == cursorRow) {
            /* Same row as host cursor: prefer suffix append.  If the
             * old text isn't a prefix of the new one (backspace,
             * line edit, or cursor-only difference we somehow
             * still see), rewrite the line in place. */
            if (newText.size() >= oldText.size() &&
                std::memcmp(newText.data(), oldText.data(),
                            oldText.size()) == 0) {
                for (size_t i = oldText.size(); i < newText.size(); ++i)
                    plan.push_back({Op::Content,
                                    static_cast<uint8_t>(newText[i])});
            } else {
                plan.push_back({Op::Carriage, 0});
                for (int i = 0; i < cursorCol; ++i)
                    plan.push_back({Op::Space, 0});
                plan.push_back({Op::Carriage, 0});
                for (uint8_t c : newText)
                    plan.push_back({Op::Content, c});
            }
            cursorCol = static_cast<int>(newText.size());
        } else {
            /* r < cursorRow — we'd have to scroll the host
             * backwards, which `\n`-only output cannot do.  Bail. */
            return false;
        }
    }

    /* Plan validated — execute through the real emit helpers.
     * Control chars (`\n`, `\r`, ` `) go through emitChar; content
     * bytes go through emitKoi8 so KOI-8R Cyrillic codes turn into
     * proper multi-byte UTF-8 in history_. */
    for (const Op &op : plan) {
        switch (op.kind) {
            case Op::Newline:  emitChar('\n');     break;
            case Op::Carriage: emitChar('\r');     break;
            case Op::Space:    emitChar(' ');      break;
            case Op::Content:  emitKoi8(op.koi8);  break;
        }
    }

    if (cursorRow > lastEmitRow_)
        lastEmitRow_ = cursorRow;
    /* Track the line content the host cursor is parked on, so the
     * dedup fallback can detect suffix extensions on it. */
    if (cursorRow >= 0)
        lastEmittedLine_ = trimmedRow(cur, cursorRow);
    if (out_) std::fflush(out_);
    return true;
}

void Terminal::emitDedup(const Snapshot &cur)
{
    /* Build the set of trimmed lines visible in shadow — these have
     * already been emitted and shouldn't appear again on the wire. */
    std::unordered_set<std::string> shadowLines;
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        auto t = trimmedRow(shadow_, r);
        if (!t.empty()) shadowLines.insert(std::move(t));
    }

    /* First pass — collect the non-blank rows of `cur` along with
     * their original row positions, and classify each as preserved
     * (already in shadow) or new.  Two patterns trigger a "full
     * screen redraw" treatment:
     *
     *   - Re-layout: a preserved row appears AFTER a new row in walk
     *     order, meaning the OS slipped a new line *above* something
     *     we'd already emitted.  Linear `\n`-only output can't
     *     represent that.
     *
     *   - All-new: not a single row of `cur` overlaps with the
     *     previous shadow (the OS cleared and reprinted from
     *     scratch — DIR, CLS, a different program taking over).
     *     Without the marker the new content would just append to
     *     scrollback with no visual break; with it, the UI scrolls
     *     the new screen to the top of the viewport so it looks
     *     like the OS terminal redraw it actually was.
     *
     * We retain `row.idx` so the emitter can preserve the OS's row
     * spacing (blank rows between content) — without it scrollback
     * would squash three lines of "© 1992", blank, ".SET TT QUIET"
     * into "© 1992\n.SET TT QUIET", losing the visual layout. */
    struct Row { int idx; std::string text; bool preserved; };
    std::vector<Row> rows;
    rows.reserve(ScreenReader::kRows);
    bool sawNew      = false;
    bool sawPreserved = false;
    bool isRelayout  = false;
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        auto t = trimmedRow(cur, r);
        if (t.empty()) continue;
        const bool preserved = shadowLines.count(t) > 0;
        if (preserved && sawNew)  isRelayout  = true;
        if (preserved)            sawPreserved = true;
        if (!preserved)           sawNew       = true;
        rows.push_back({r, std::move(t), preserved});
    }
    /* Decide whether the snapshot deserves "fresh screen" treatment
     * (blank-line separator + lastScreenStart_ advance, so the UI
     * scrolls the new content to viewport top) or should land as a
     * plain append below the existing scrollback.
     *
     *   - cur has ≥3 non-blank rows      → big new screen (POST,
     *                                       boot banner, DIR output);
     *                                       redraw.
     *   - shadow had ≥3 non-blank rows   → the OS just wiped a full
     *                                       screen and replaced it
     *                                       with a small prompt
     *                                       (e.g. "Время [чч:мм]?"
     *                                       after DIR); redraw.
     *   - both are small (e.g. "© 1992
     *     Родионов С.А." after "НГМД
     *     готов...")                    → append.  Keep the
     *                                       previous anchor so the
     *                                       user's view of "НГМД
     *                                       готов..." stays pinned
     *                                       at the top until enough
     *                                       content accumulates
     *                                       below it. */
    int shadowNonBlank = 0;
    for (int r = 0; r < ScreenReader::kRows; ++r)
        if (!trimmedRow(shadow_, r).empty()) ++shadowNonBlank;
    const bool allNew = !rows.empty() && !sawPreserved
                     && (rows.size() >= 3 || shadowNonBlank >= 3);

    if (isRelayout || allNew) {
        /* Reprint the current screen so scrollback shows the layout
         * the way the OS sees it.  A clearly-marked separator line
         * sits between the previous on-wire state and the new
         * screen so a user scrolling back through scrollback can
         * see exactly where each redraw happened.  lastScreenStart_
         * marks the byte offset of the new screen's first line so
         * the UI can scroll it to viewport top — the separator
         * lands one line above (just out of view).  Gaps between
         * non-blank rows are reproduced via extra newlines so the
         * OS's row spacing is preserved. */
        /* 80-column separator (screen width) with " screen redrawn "
         * centred — 32 dashes + 16-char label + 32 dashes = 80. */
        emitText("\n"
                 "-------------------------------- screen redrawn "
                 "--------------------------------\n");
        lastScreenStart_ = history_.size();
        /* Reproduce the OS row layout faithfully: emit row.idx
         * newlines before the first non-blank row's content (so any
         * blank rows above it on the OS screen become blank lines
         * in scrollback), then row.idx-prevIdx newlines between
         * subsequent rows.  Without this, blank rows at the top of
         * a freshly-drawn screen (e.g. POST starting with three
         * empty rows above the "Электроника" banner) get collapsed
         * and the visual layout differs from what the OS shows. */
        int prevIdx = 0;
        bool first = true;
        for (const auto &row : rows) {
            const int gap = first ? row.idx : (row.idx - prevIdx);
            for (int i = 0; i < gap; ++i) emitChar('\n');
            for (uint8_t c : row.text) emitKoi8(c);
            prevIdx = row.idx;
            first = false;
        }
        if (!rows.empty())
            lastEmittedLine_ = rows.back().text;
    } else {
        /* Linear dedup: emit only the new lines.  Suffix extension
         * (the new line starts with the last one we put on the
         * wire) writes just the appended tail; otherwise the new
         * line lands at the host cursor's expected row gap.  Gaps
         * between cur rows (preserved or absent) are reproduced via
         * extra newlines so blank-line separators in the OS's
         * layout reach scrollback intact. */
        int lastIdx = -1;
        for (const auto &row : rows) {
            if (row.preserved) {
                lastIdx = row.idx;
                continue;
            }
            const bool suffixExt = !lastEmittedLine_.empty() &&
                row.text.size() > lastEmittedLine_.size() &&
                std::memcmp(row.text.data(), lastEmittedLine_.data(),
                            lastEmittedLine_.size()) == 0;
            if (suffixExt) {
                for (size_t i = lastEmittedLine_.size();
                     i < row.text.size(); ++i)
                    emitKoi8(static_cast<uint8_t>(row.text[i]));
            } else {
                const int gap = lastIdx >= 0 ? row.idx - lastIdx : 1;
                for (int i = 0; i < gap; ++i) emitChar('\n');
                for (uint8_t c : row.text) emitKoi8(c);
            }
            lastIdx = row.idx;
            lastEmittedLine_ = row.text;
        }
    }

    if (out_) std::fflush(out_);
    shadow_      = cur;
    lastEmitRow_ = lastNonEmptyRow(cur);
}

/*
 * Helper: emit a sequence of non-blank rows separated by `\n`, no
 * leading or trailing newline.  Used by initial dump and redraw
 * paths so the buffer never accumulates a stray empty line at the
 * tail (which would leave lastEmitRow_ inconsistent with the host
 * cursor's actual position). */
static void emitRowsSeparated(Terminal &t, const ScreenReader::Snapshot &snap,
                              auto &&rowText, auto &&emitNl, auto &&emitText)
{
    bool first = true;
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        const auto text = rowText(snap, r);
        if (text.empty()) continue;
        if (!first) emitNl();
        first = false;
        emitText(text);
    }
    (void)t;
}

void Terminal::feedSample(const ScreenReader::Snapshot &snap)
{
    /* Gate 1: clean — no unknown-glyph cells (mid-bitmap-rewrite
     * produces partial keys that decode as kUnknownGlyph). */
    bool clean = true;
    for (uint8_t code : snap.cells) {
        if (code == ScreenReader::kUnknownGlyph) {
            clean = false;
            break;
        }
    }

    /* Gate 2: progressing — no row in `snap` is a strict-subset
     * partial copy of *any* row in the last forwarded snap.
     * Mid-scroll routines copy content from one row into another
     * cell-by-cell; the intermediate state has cur.row[R] looking
     * like a partial copy of lastForwardedSnap_.row[R'] for some R'.
     * Strict subset = cur's non-blanks are at the same positions
     * as ref's with the same values, and cur has fewer non-blanks.
     * Reject those — they're mid-scroll-copy garbage.
     *
     * Stability across two consecutive samples was tried as a third
     * gate but rejected almost every mid-DIR frame at per-emu-frame
     * sampling (consecutive samples differ by one printed line).
     * Letting non-stable but clean+progressing samples through is
     * what allows files 1-2 of a fast DIR to reach scrollback before
     * scrolling pushes them off the OS screen. */
    /* Treat cursor cells (any character listed in transparentChars_)
     * as blank when comparing — otherwise cursor blink (cell
     * toggling between '_' and ' ') makes a row look like a strict
     * subset of itself-with-cursor and trips the progressing gate
     * every time the user is at a prompt.  All forwards get
     * rejected, and the next clean state we eventually accept is
     * far past the prompt — which is exactly what produced the
     * spurious "screen redrawn" separators between successive
     * commands. */
    auto stripTransparent = [&](uint8_t c) -> uint8_t {
        return transparentChars_.find(static_cast<char>(c))
                 != std::string::npos ? uint8_t(0x20) : c;
    };

    bool progressing = true;
    if (clean && hasLastForwardedSnap_
     && lastForwardedSnap_.cols == snap.cols) {
        for (int r = 0; r < ScreenReader::kRows && progressing; ++r) {
            const uint8_t *cur = &snap.cells[r * ScreenReader::kHiresCols];
            int curNonBlank = 0;
            for (int c = 0; c < snap.cols; ++c)
                if (stripTransparent(cur[c]) != 0x20) ++curNonBlank;
            if (curNonBlank < 3) continue;
            for (int rr = 0; rr < ScreenReader::kRows; ++rr) {
                const uint8_t *ref = &lastForwardedSnap_.cells[
                    rr * ScreenReader::kHiresCols];
                int refNonBlank = 0;
                bool subset = true;
                for (int c = 0; c < snap.cols; ++c) {
                    const uint8_t curC = stripTransparent(cur[c]);
                    const uint8_t refC = stripTransparent(ref[c]);
                    if (refC != 0x20) ++refNonBlank;
                    if (curC != 0x20 && refC != curC) {
                        subset = false;
                        break;
                    }
                }
                if (subset && curNonBlank < refNonBlank) {
                    progressing = false;
                    break;
                }
            }
        }
    }

    /* Gate 3: no-adjacent-duplicate — two consecutive non-blank
     * rows with identical trimmed content are the signature of an
     * in-progress scroll-up that copied row R+1 to row R but
     * hasn't yet cleared row R+1 (the original).  detectScrollUp
     * would happily match (cur[i] == shadow[i+1] holds) and emit
     * cur's bottom row as "new content" — except it's the SAME
     * row that's already at row R, so we'd duplicate it in
     * scrollback (the "MORDA .SCR ... / MORDA .SCR ..." pattern
     * the user reported).  The OS finishes the clear within a few
     * cycles; skip this sample and the next clean one will be the
     * proper post-scroll state. */
    bool noAdjacentDup = true;
    if (clean && progressing) {
        for (int r = 0; r + 1 < ScreenReader::kRows; ++r) {
            const auto a = trimmedRow(snap, r);
            if (a.empty()) continue;
            if (trimmedRow(snap, r + 1) == a) {
                noAdjacentDup = false;
                break;
            }
        }
    }

    if (clean && progressing && noAdjacentDup) {
        update(snap);
        lastForwardedSnap_    = snap;
        hasLastForwardedSnap_ = true;
    }
}

void Terminal::update(const ScreenReader::Snapshot &snap)
{
    /* Drop snapshots dominated by unknown-glyph cells.  At cold-boot
     * VRAM is full of 0xFF bytes — every cell decodes to
     * kUnknownGlyph and would otherwise land in scrollback as 25
     * lines of solid █.  Diag run on Rodionov OSA-B confirmed this
     * (frames 30–60 were entirely full-block).  A real OS screen
     * has at most a handful of unknowns (custom-bitmap glyphs the
     * font map can't decode, e.g. ©), so a threshold of >100 cleanly
     * separates "uninitialised VRAM" from "OS text with a few
     * unknown glyphs". */
    int unknownCount = 0;
    for (uint8_t code : snap.cells)
        if (code == ScreenReader::kUnknownGlyph) ++unknownCount;
    if (unknownCount > 100)
        return;

    /* Drop snapshots that have any single row peppered with multiple
     * unknown-glyph cells.  Real OS-drawn lines are clean ASCII +
     * Cyrillic with at most one or two custom glyphs (e.g. ©), so a
     * row with 3+ unknown cells is the signature of a mid-scroll /
     * mid-redraw moment where the OS copied some cells of a row but
     * not others — sampling caught the row half-rewritten with stale
     * pixel patterns from the previous content.  Forwarding those
     * leaks "█    .S█        █-0 -██"-style garbage into scrollback
     * AND triggers a spurious redraw separator (the corrupted line
     * has no match in shadow → isRelayout fires when subsequent
     * preserved rows follow it). */
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        int rowUnknowns = 0;
        for (int c = 0; c < snap.cols; ++c) {
            if (snap.cells[r * ScreenReader::kHiresCols + c]
                == ScreenReader::kUnknownGlyph)
                ++rowUnknowns;
        }
        if (rowUnknowns >= 3)
            return;
    }

    /* Drop fully-blank snapshots.  When the OS transitions between
     * screens it clears VRAM mid-redraw — a snapshot landing in
     * that window has every row blank.  Letting it through would
     * overwrite shadow_ with the cleared state, and the very next
     * non-empty snapshot (e.g. "НГМД готов...") would see an
     * empty shadow → allNew fails the (shadow≥3) check → no
     * redraw fires → the previous anchor remains in force, and
     * auto-scroll-to-tail eventually pushes the new "first line"
     * off the top of the viewport.  Skipping the blank moment
     * preserves the previous shadow so the next emit correctly
     * triggers the screen-clear-and-replace path. */
    bool anyContent = false;
    for (int r = 0; r < ScreenReader::kRows; ++r) {
        if (!trimmedRow(snap, r).empty()) { anyContent = true; break; }
    }
    if (!anyContent)
        return;

    /* First frame: dump the current visible content (skipping
     * blank rows entirely so we don't pad scrollback with empty
     * lines).  This becomes the baseline shadow.  No trailing
     * newline — lastEmitRow_ tracks the row of the *last emitted
     * character*, so the host cursor sits at the end of the last
     * non-empty row's content. */
    if (!hasShadow_) {
        /* The initial dump is the first "screen" the UI should
         * anchor against. */
        lastScreenStart_ = history_.size();
        emitRowsSeparated(
            *this, snap,
            [&](const Snapshot &s, int r){ return trimmedRow(s, r); },
            [&]{ emitChar('\n'); },
            [&](const std::string &t){
                for (uint8_t c : t) emitKoi8(c);
            });
        if (out_) std::fflush(out_);
        shadow_           = snap;
        hasShadow_        = true;
        lastEmitRow_      = lastNonEmptyRow(snap);
        lastEmittedLine_  = lastEmitRow_ >= 0
                          ? trimmedRow(snap, lastEmitRow_)
                          : std::string{};
        return;
    }

    if (isUnchanged(snap))
        return;

    /* Try scroll first — it's the cleanest representation when it
     * fits, and the host's native scrollback gets to keep the
     * lines that fell off the top.  Each new-bottom row is emitted
     * with a leading `\n` so the host cursor advances onto a fresh
     * line before the row's content lands. */
    if (int k = detectScrollUp(snap); k > 0) {
        for (int i = 0; i < k; ++i) {
            emitChar('\n');
            emitRowText(snap, ScreenReader::kRows - k + i);
        }
        if (out_) std::fflush(out_);
        shadow_           = snap;
        lastEmitRow_      = ScreenReader::kRows - 1;
        lastEmittedLine_  = trimmedRow(snap, ScreenReader::kRows - 1);
        return;
    }

    /* Try the row-by-row incremental plan: handles single-row
     * appends, multi-row "OS just printed three lines below the
     * prompt" bursts, and in-place line edits. */
    if (tryEmitIncremental(snap)) {
        shadow_ = snap;
        return;
    }

    /* Fallback: re-layout, partial scroll with suffix extension, or
     * any other change pattern the incremental plan can't express
     * forward-only.  Set-diff emits only the lines that aren't
     * already on the wire — no `[--- screen redrawn ---]` marker,
     * no duplication of preserved content. */
    emitDedup(snap);
}

} /* namespace ms0515 */
