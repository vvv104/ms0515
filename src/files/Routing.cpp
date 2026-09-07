/*
 * Routing.cpp — the NC rule, applied.
 */
#include "Routing.hpp"

namespace ms0515::files {

namespace {

constexpr uint8_t kEnter = 0x0D;
constexpr uint8_t kTab = 0x09;
constexpr uint8_t kEsc = 0x1B;
constexpr uint8_t kCtrlC = 0x03;
constexpr uint8_t kCtrlU = 0x15;

bool printable(uint8_t b) noexcept
{
    return b >= 0x20 && b != 0x7F;
}

} // namespace

Route routeKey(const HostKey &key, const RouteState &state) noexcept
{
    if (key.isByte() && key.byte == kToggleByte) return state.commanderOn ? Route::commander : Route::toggle;
    if (!state.commanderOn) return Route::guest;
    if (state.modal) return Route::commander;
    if (key.isByte() && key.byte == kHidePanelsByte) return Route::hidePanels;
    if (state.panelsHidden) {
        /* with the machine's screen alone, PgUp / PgDn leaf through the
         * rows that left it - keys the machine has none of */
        const bool leaf = key.special == SpecialKey::pageUp || key.special == SpecialKey::pageDown;
        return leaf ? Route::commander : Route::guest;
    }
    if (key.isSpecial()) return Route::commander;
    switch (key.byte) {
    case kEnter: return state.typedPending ? Route::guest : Route::commander;
    case kTab:
    case kEsc:   return Route::commander;
    /* mc's select / unselect / invert: the panel's while nothing is typed */
    case '+':
    case '-':
    case '*':    return state.typedPending ? Route::guest : Route::commander;
    default:     return Route::guest;
    }
}

RouteState afterRouting(RouteState state, const HostKey &key, Route route) noexcept
{
    if (route != Route::guest || !key.isByte()) return state;
    if (printable(key.byte)) state.typedPending = true;
    else if (key.byte == kEnter || key.byte == kCtrlU || key.byte == kCtrlC) state.typedPending = false;
    return state;
}

} /* namespace ms0515::files */
