/*
 * Scrollback.hpp — the rows that leave the machine's screen at the top,
 * kept for the host to show above it.
 *
 * The MS-0515 has no scrollback: the OS moves the rows up in VRAM and
 * the top one is gone.  Given the mirror's shadow after every frame that
 * changed it, this tells a scroll from printing in place and keeps the
 * rows that left.  A frame may catch the OS half-way through the move -
 * the upper rows already copied up, the rest not yet, or one row half
 * copied - so a frame is read as a scroll only when the whole screen
 * agrees on the shift (blank rows below the content excepted, which new
 * text may fill), a half-done move is left for the next frame, and a
 * row that was torn is still counted once, never twice.  A screen drawn
 * anew (a program clearing it) keeps nothing.
 */
#ifndef MS0515_CLI_SCROLLBACK_HPP
#define MS0515_CLI_SCROLLBACK_HPP

#include <ms0515/VramMirror.hpp>

#include <array>
#include <vector>

namespace ms0515::cli {

struct ScrollLine {
    std::array<uint8_t, VramMirror::kCols> cells{};
    std::array<bool, VramMirror::kCols> inverted{};
};

class Scrollback {
public:
    /* The screen after a frame that changed it. */
    void frame(const VramMirror::Snapshot &screen);
    [[nodiscard]] const std::vector<ScrollLine> &lines() const noexcept { return lines_; }
    void clear() noexcept { lines_.clear(); haveRef_ = false; }

private:
    bool haveRef_ = false;
    VramMirror::Snapshot ref_;     /* the last screen read as settled */
    std::vector<ScrollLine> lines_;

    void keep(const VramMirror::Snapshot &s, int row);
    [[nodiscard]] static bool sameRow(const VramMirror::Snapshot &a, int ra, const VramMirror::Snapshot &b, int rb) noexcept;
    [[nodiscard]] static bool blankRow(const VramMirror::Snapshot &s, int row) noexcept;
    [[nodiscard]] bool wholeShift(const VramMirror::Snapshot &cur, int k) const noexcept;
    [[nodiscard]] bool halfDoneShift(const VramMirror::Snapshot &cur) const noexcept;
    [[nodiscard]] bool tornShift(const VramMirror::Snapshot &cur) const noexcept;
};

} /* namespace ms0515::cli */

#endif /* MS0515_CLI_SCROLLBACK_HPP */
