/*
 * Routing.hpp — who gets a key while the commander sits over the machine.
 *
 * The Norton Commander rule, with the guest's own prompt as the command
 * line: what you type - letters, Backspace, the Ctrl+letters RT-11 knows
 * (^C, ^U, ^S ...) - goes to the machine even with the panels up, and so
 * does Enter once something was typed; Tab, the arrows, Insert, Home,
 * End, PgUp, PgDn, Esc, the F-keys and an untyped Enter work the panels,
 * and so do + - * (select, unselect, invert) while nothing is typed.
 * Ctrl+O hides the panels, and then every key is the guest's but PgUp
 * and PgDn, which leaf through the rows that left the screen; Ctrl+\
 * (0x1C, the one byte every terminal delivers as itself and RT-11 never
 * uses) brings the commander up - F10 takes it down, and while it is up
 * Ctrl+\ is swallowed.  A dialog or the viewer takes every key while it
 * is open.
 */
#ifndef MS0515_FILES_ROUTING_HPP
#define MS0515_FILES_ROUTING_HPP

#include "HostKey.hpp"

namespace ms0515::files {

constexpr uint8_t kToggleByte = 0x1C;     /* Ctrl+\ */
constexpr uint8_t kHidePanelsByte = 0x0F; /* Ctrl+O */

enum class Route { guest, commander, toggle /* the commander up */, hidePanels };

struct RouteState {
    bool commanderOn = false;
    bool panelsHidden = false;   /* Ctrl+O: the guest's screen alone */
    bool modal = false;          /* a dialog or the viewer is open */
    bool typedPending = false;   /* text went to the guest since its last Enter */
};

[[nodiscard]] Route routeKey(const HostKey &key, const RouteState &state) noexcept;

/* The state after `key` went where `route` says: tracks the typed text
 * (set by a printable byte, cleared by Enter, ^U and ^C to the guest). */
[[nodiscard]] RouteState afterRouting(RouteState state, const HostKey &key, Route route) noexcept;

} /* namespace ms0515::files */

#endif /* MS0515_FILES_ROUTING_HPP */
