/*
 * GuestScreen.hpp — the machine's text screen as FTXUI rows.
 *
 * The VramMirror keeps the 80x25 shadow of the hires text plane the
 * guest draws into; while the commander is up that screen is not on the
 * terminal any more, so it is drawn from the shadow: whole, when the
 * panels are hidden (Ctrl+O), or its rows around the guest's cursor
 * under the panels - NC's command line, here the machine's own prompt.
 */
#ifndef MS0515_CLI_GUEST_SCREEN_HPP
#define MS0515_CLI_GUEST_SCREEN_HPP

#include <ms0515/VramMirror.hpp>

#include <ftxui/dom/elements.hpp>

namespace ms0515::cli {

/* Rows `from` .. `to` (exclusive) of the snapshot, clipped to the screen,
 * each 80 cells, the inverted cells inverted; the cell at (cursorRow,
 * cursorCol) - the guest's cursor, which the mirror keeps blank - drawn
 * inverted so the prompt shows where the typing goes. */
[[nodiscard]] ftxui::Element guestRows(const VramMirror::Snapshot &snapshot, int from, int to,
                                       int cursorRow = -1, int cursorCol = -1);

/* Where the guest's screen goes when the panels are hidden: its cursor
 * row on the terminal row it has under the panels (two above the key
 * bar), so the prompt does not jump; `pad` blank rows above, the guest's
 * rows `from` .. `to` (one below the cursor at most). */
struct GuestPlacement {
    int pad = 0;
    int from = 0;
    int to = 0;
};
[[nodiscard]] GuestPlacement placeGuest(int cursorRow, int height);

} /* namespace ms0515::cli */

#endif /* MS0515_CLI_GUEST_SCREEN_HPP */
