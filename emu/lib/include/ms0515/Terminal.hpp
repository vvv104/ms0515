/*
 * Terminal.hpp — Output-only host-terminal mirror.
 *
 * Sits on top of ScreenReader and replays the emulated OS's text
 * screen as a linear stream of host-terminal output.  Its job is to
 * recognise the three diff patterns that show up while the OS is at
 * a command prompt:
 *
 *   1. Append    — characters added at the end of one row (echoed
 *                  user input or freshly printed output).
 *   2. Scroll-up — top rows fall off, the bottom rows are new
 *                  (OS printed past the last screen line).
 *   3. Redraw    — anything else (clear-screen, big rewrite, mode
 *                  change).  Emits a marker and the current screen.
 *
 * What it intentionally does NOT do:
 *   - Position the host-terminal cursor with ANSI escapes.  We rely
 *     on the host's natural left-to-right / top-to-bottom flow plus
 *     `\n` so scrolled-off lines fall into the host's native scroll
 *     buffer.  That gives the user back the scrollback the MS-0515
 *     does not have on its own 25-line display.
 *   - Reflect cursor blink (the OS draws a '_' that toggles on/off).
 *     The cursor character is treated as a transparent blank for
 *     diffing — see `setCursorChar`.
 *
 * Caller wires it up by feeding a freshly-read Snapshot every frame
 * (or on a slower cadence — the diff handles arbitrary deltas).
 * Phase 1 of the terminal feature: output only; keyboard injection
 * back into the OS will land separately.
 */

#ifndef MS0515_TERMINAL_HPP
#define MS0515_TERMINAL_HPP

#include <ms0515/ScreenReader.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace ms0515 {

class Terminal {
public:
    Terminal();

    /* Set the host-side stream.  Pass nullptr to disable output —
     * `update()` becomes a no-op. */
    void setOutput(FILE *f) noexcept { out_ = f; }

    /* Override the set of "transparent" characters — code points
     * collapsed to a space when computing the trimmed row used for
     * diff classification.  The default is the OS-drawn cursor
     * (`_` for Omega/OSA, possibly other underscore-like glyphs
     * elsewhere) — without stripping it, every cursor blink and
     * every cursor move between rows would register as a content
     * change and trip the incremental classifier into redraws. */
    void setTransparentChars(std::string_view chars)
        { transparentChars_ = chars; }

    /* Backwards-compatible single-char setter — sets the
     * transparent-char set to exactly `c`. */
    void setCursorChar(uint8_t c) {
        transparentChars_.assign(1, static_cast<char>(c));
    }

    /* Drop the shadow so the next update() emits the full current
     * screen as the new initial state. */
    void reset() noexcept;

    /* Compare `snap` against the previous frame and emit just the
     * delta to `out_` and the in-memory history.  Trailing blanks
     * are always stripped — the OS pre-fills its 80×25 cell grid
     * with spaces; the host terminal sees only meaningful
     * characters. */
    void update(const ScreenReader::Snapshot &snap);

    /* Read-only access to every byte the mirror has ever emitted.
     * Returned as `const std::string&` (rather than string_view) so
     * UI code can hand the underlying null-terminated buffer to
     * ImGui::InputTextMultiline.  The string is the same UTF-8
     * stream that goes to `out_` (if configured), so a UI window
     * opened mid-session can show the full scrollback by rendering
     * it directly. */
    [[nodiscard]] const std::string &history() const noexcept
        { return history_; }

    /* Discard the accumulated history.  Does not touch the shadow
     * (subsequent diffs continue to compare against the latest
     * snapshot); use reset() if you want both. */
    void clearHistory() noexcept
        { history_.clear(); lastScreenStart_ = 0; }

    /* Byte offset in history() where the most recent "full-screen
     * redraw" begins.  Updated by the initial dump and by the dedup
     * re-layout path — every time the OS rearranges the screen and
     * we replay it from the top.  UI code uses this to anchor the
     * scrollback view to the start of the new screen so it visually
     * mimics an OS terminal redraw. */
    [[nodiscard]] std::size_t lastScreenStart() const noexcept
        { return lastScreenStart_; }

private:
    using Snapshot = ScreenReader::Snapshot;

    /* Length of `s.row(r)` after stripping trailing blanks AND
     * treating cursorChar_ as a blank (so cursor positioning does
     * not affect the trimmed length).  Returns the byte string
     * directly because every caller wants both the length and the
     * content. */
    std::string trimmedRow(const Snapshot &s, int r) const;

    /* Returns k > 0 if the new screen is the shadow scrolled up by
     * exactly k rows (so shadow.row(i+k) == new.row(i) for i < 25-k)
     * AND the preserved overlap actually contains some non-blank
     * content (otherwise the match is meaningless — a screen that
     * was mostly empty matches itself trivially at any k).  Returns
     * 0 if no useful scroll is found.  Cursor character is stripped
     * before comparison. */
    int detectScrollUp(const Snapshot &cur) const;

    /* True if shadow and cur agree on every trimmed row, i.e. no
     * visible change once cursor blink and trailing blanks are
     * ignored. */
    bool isUnchanged(const Snapshot &cur) const;

    /* Try to express the diff as an in-order sequence of row
     * updates: for each row R that changed, R is either the
     * current host-cursor row (suffix appended in place) or it is
     * strictly past it (a fresh row, possibly skipping intermediate
     * blank rows).  If a changed row would require moving the host
     * cursor backwards, the function bails out and returns false so
     * the caller falls through to the dedup path.  All actual
     * emission goes through emitChar/emitText/emitKoi8 so history_
     * and out_ both advance. */
    bool tryEmitIncremental(const Snapshot &cur);

    /* Set-diff fallback: emit only the lines from `cur` that aren't
     * already present in the shadow (treating each non-blank
     * trimmed row as a unit of content).  Handles two patterns the
     * row-by-row incremental can't:
     *
     *   1. Re-layout — same lines arranged differently (e.g. POST
     *      first draws sparse with blank rows between entries, then
     *      condenses into a packed block plus a new heading line).
     *      All preserved lines skip silently; only the genuinely
     *      new ones reach the wire.
     *
     *   2. Scroll with suffix-extension — the bottom row of the
     *      preserved overlap was being printed when we last sampled
     *      ("^T Auto RUN D") and is now complete ("^T Auto RUN
     *      DBAS ALL").  detectScrollUp's strict equality misses it.
     *      Here we recognise that a new line starts with the last
     *      one we emitted and write only the appended tail.
     *
     * Output is always linear (`\n` separators, no cursor
     * positioning) so the host's native scrollback receives the
     * stream the user expects to read top-to-bottom. */
    void emitDedup(const Snapshot &cur);

    /* Emit the trimmed text of `row` from `snap` followed by a
     * trailing newline.  KOI-8 → UTF-8 translation goes through
     * ScreenReader::putKoi8Char. */
    void emitRowLine(const Snapshot &snap, int row);

    /* Just the trimmed text, no trailing newline. */
    void emitRowText(const Snapshot &snap, int row);

    /* Index of the highest non-empty trimmed row, or -1 if the
     * snapshot has no visible content. */
    int lastNonEmptyRow(const Snapshot &s) const;

    /* Internal helpers that fan output to both the FILE* (if any)
     * and the in-memory history buffer.  All emit code paths in
     * Terminal.cpp go through these so neither sink can drift. */
    void emitChar(char c);
    void emitText(std::string_view s);
    void emitKoi8(uint8_t koi8);

    Snapshot    shadow_;
    bool        hasShadow_       = false;
    std::string transparentChars_ = "_";
    FILE       *out_             = nullptr;
    std::string history_;

    /* Logical row of the shadow that the host terminal's cursor is
     * currently parked on.  -1 before the very first emit; updated
     * by every emit path so multi-row append knows how many `\n`s
     * to issue to advance to a fresh row. */
    int         lastEmitRow_     = -1;

    /* Trimmed content of the line the host cursor is currently
     * parked on.  Used by the dedup fallback to detect suffix
     * extensions ("^T … RUN D" → "^T … RUN DBAS ALL"): if a new
     * line in cur starts with this string, we emit just the
     * appended tail instead of duplicating the prefix. */
    std::string lastEmittedLine_;

    /* Byte offset in history_ where the most recent "screen redraw"
     * starts — updated by the initial dump and by the dedup
     * re-layout path.  Exposed via lastScreenStart() for UI use. */
    std::size_t lastScreenStart_ = 0;
};

} /* namespace ms0515 */

#endif /* MS0515_TERMINAL_HPP */
