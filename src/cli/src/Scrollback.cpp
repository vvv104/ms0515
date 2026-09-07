/*
 * Scrollback.cpp — a scroll told from printing, half-done moves left
 * alone, the rows that left kept once.
 */
#include "Scrollback.hpp"

#include <algorithm>

namespace ms0515::cli {

namespace {

constexpr int kRows = VramMirror::kRows;
constexpr int kCols = VramMirror::kCols;

} // namespace

bool Scrollback::sameRow(const VramMirror::Snapshot &a, int ra, const VramMirror::Snapshot &b, int rb) noexcept
{
    const size_t oa = static_cast<size_t>(ra) * kCols, ob = static_cast<size_t>(rb) * kCols;
    return std::equal(a.cells.begin() + static_cast<long>(oa), a.cells.begin() + static_cast<long>(oa + kCols), b.cells.begin() + static_cast<long>(ob))
        && std::equal(a.inverted.begin() + static_cast<long>(oa), a.inverted.begin() + static_cast<long>(oa + kCols), b.inverted.begin() + static_cast<long>(ob));
}

bool Scrollback::blankRow(const VramMirror::Snapshot &s, int row) noexcept
{
    const size_t o = static_cast<size_t>(row) * kCols;
    for (size_t i = o; i < o + kCols; ++i)
        if (s.cells[i] != 0x20 && s.cells[i] != 0) return false;
    return true;
}

void Scrollback::keep(const VramMirror::Snapshot &s, int row)
{
    ScrollLine line;
    const size_t o = static_cast<size_t>(row) * kCols;
    std::copy(s.cells.begin() + static_cast<long>(o), s.cells.begin() + static_cast<long>(o + kCols), line.cells.begin());
    std::copy(s.inverted.begin() + static_cast<long>(o), s.inverted.begin() + static_cast<long>(o + kCols), line.inverted.begin());
    lines_.push_back(line);
}

/* Every row of `cur` is the row `k` below it in the reference - or that
 * reference row was blank and new text may stand there now - and at
 * least one row of content did move. */
bool Scrollback::wholeShift(const VramMirror::Snapshot &cur, int k) const noexcept
{
    bool evidence = false;
    for (int r = 0; r + k < kRows; ++r) {
        if (sameRow(cur, r, ref_, r + k)) { if (!blankRow(ref_, r + k)) evidence = true; continue; }
        if (!blankRow(ref_, r + k)) return false;
    }
    return evidence;
}

/* The upper rows moved up by one, the rows from some point down are
 * still the old ones: the OS is in the middle of the move. */
bool Scrollback::halfDoneShift(const VramMirror::Snapshot &cur) const noexcept
{
    for (int j = 1; j < kRows; ++j) {
        bool upperMoved = true, lowerOld = true, evidence = false;
        for (int r = 0; r < j && upperMoved; ++r) {
            upperMoved = sameRow(cur, r, ref_, r + 1);
            if (upperMoved && !blankRow(ref_, r + 1) && !sameRow(cur, r, ref_, r)) evidence = true;
        }
        for (int r = j; r < kRows && lowerOld; ++r) lowerOld = sameRow(cur, r, ref_, r);
        if (upperMoved && lowerOld && evidence) return true;
    }
    return false;
}

/* The upper rows moved up by one with content in them, and the rest is
 * neither the old rows nor the moved ones: a row was caught torn.  The
 * top row did leave. */
bool Scrollback::tornShift(const VramMirror::Snapshot &cur) const noexcept
{
    bool evidence = false;
    for (int r = 0; r + 1 < kRows && sameRow(cur, r, ref_, r + 1); ++r)
        if (!blankRow(ref_, r + 1) && !sameRow(cur, r, ref_, r)) evidence = true;
    return evidence;
}

void Scrollback::frame(const VramMirror::Snapshot &screen)
{
    if (!haveRef_) { ref_ = screen; haveRef_ = true; return; }
    if (screen.cells == ref_.cells && screen.inverted == ref_.inverted) return;
    for (int k = 1; k < kRows; ++k) {
        if (!wholeShift(screen, k)) continue;
        for (int r = 0; r < k; ++r) keep(ref_, r);
        ref_ = screen;
        return;
    }
    if (halfDoneShift(screen)) return;          /* the next frame will have the whole of it */
    if (tornShift(screen)) keep(ref_, 0);
    ref_ = screen;                              /* printed in place, or drawn anew */
}

} /* namespace ms0515::cli */
