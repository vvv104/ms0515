/*
 * test_routing.cpp — who gets a key while the commander sits over the
 * machine: the NC rule.  Typed text and Enter go to the guest's prompt,
 * the panel keys to the commander; Ctrl+\ brings the commander up (F10
 * takes it down); Ctrl+O hides the
 * panels and then everything goes to the guest.
 */
#include "HostKey.hpp"
#include "Routing.hpp"

#include <doctest/doctest.h>

using namespace ms0515::files;

namespace {

RouteState on()
{
    RouteState s;
    s.commanderOn = true;
    return s;
}

} // namespace

TEST_CASE("Ctrl+\\ brings the commander up, and is the commander's (swallowed) while it is up; with it off everything else is the guest's")
{
    const HostKey toggle = HostKey::ofByte(0x1C);
    CHECK(routeKey(toggle, RouteState{}) == Route::toggle);
    CHECK(routeKey(toggle, on()) == Route::commander);
    RouteState modal = on();
    modal.modal = true;
    CHECK(routeKey(toggle, modal) == Route::commander);

    CHECK(routeKey(HostKey::ofByte('a'), RouteState{}) == Route::guest);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::f5), RouteState{}) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x0F), RouteState{}) == Route::guest);     /* RT-11's own ^O */
    CHECK(routeKey(HostKey::ofByte(0x0D), RouteState{}) == Route::guest);
}

TEST_CASE("panels shown: text, Backspace and Ctrl+letters go to the guest, the panel keys to the commander")
{
    const RouteState s = on();
    CHECK(routeKey(HostKey::ofByte('a'), s) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0xC1), s) == Route::guest);               /* a Cyrillic letter */
    CHECK(routeKey(HostKey::ofByte(' '), s) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x08), s) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x7F), s) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x03), s) == Route::guest);               /* ^C aborts the guest's program */
    CHECK(routeKey(HostKey::ofByte(0x15), s) == Route::guest);               /* ^U */
    CHECK(routeKey(HostKey::ofByte(0x09), s) == Route::commander);           /* Tab: the other panel */
    CHECK(routeKey(HostKey::ofByte(0x1B), s) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::up), s) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::insert), s) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::pageDown), s) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::f3), s) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::f1, true), s) == Route::commander);
    CHECK(routeKey(HostKey::ofByte(0x0F), s) == Route::hidePanels);
    /* mc's + - * work the panel while the command line is empty, and are
     * text once something is typed */
    CHECK(routeKey(HostKey::ofByte('+'), s) == Route::commander);
    CHECK(routeKey(HostKey::ofByte('-'), s) == Route::commander);
    CHECK(routeKey(HostKey::ofByte('*'), s) == Route::commander);
    RouteState typed = s;
    typed.typedPending = true;
    CHECK(routeKey(HostKey::ofByte('+'), typed) == Route::guest);
    CHECK(routeKey(HostKey::ofByte('*'), typed) == Route::guest);
}

TEST_CASE("Enter goes to the guest only when text was typed since the last Enter, else it acts on the panel")
{
    RouteState s = on();
    CHECK(routeKey(HostKey::ofByte(0x0D), s) == Route::commander);
    s = afterRouting(s, HostKey::ofByte('d'), Route::guest);
    CHECK(s.typedPending);
    s = afterRouting(s, HostKey::ofByte('i'), Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x0D), s) == Route::guest);
    s = afterRouting(s, HostKey::ofByte(0x0D), Route::guest);
    CHECK_FALSE(s.typedPending);
    CHECK(routeKey(HostKey::ofByte(0x0D), s) == Route::commander);

    /* ^U and ^C wipe the guest's line, so the pending text is gone too */
    s = afterRouting(s, HostKey::ofByte('x'), Route::guest);
    s = afterRouting(s, HostKey::ofByte(0x15), Route::guest);
    CHECK_FALSE(s.typedPending);
    s = afterRouting(s, HostKey::ofByte('x'), Route::guest);
    s = afterRouting(s, HostKey::ofByte(0x03), Route::guest);
    CHECK_FALSE(s.typedPending);

    /* a Backspace of the only character does not clear the flag - the guest
     * may still hold text we never saw typed; a key to the commander leaves
     * the flag alone */
    s = afterRouting(s, HostKey::ofByte('x'), Route::guest);
    s = afterRouting(s, HostKey::ofByte(0x08), Route::guest);
    CHECK(s.typedPending);
    s = afterRouting(s, HostKey::ofSpecial(SpecialKey::down), Route::commander);
    CHECK(s.typedPending);
}

TEST_CASE("a dialog or the viewer takes every key; hidden panels give every key to the guest but Ctrl+O and Ctrl+\\")
{
    RouteState modal = on();
    modal.modal = true;
    CHECK(routeKey(HostKey::ofByte('a'), modal) == Route::commander);
    CHECK(routeKey(HostKey::ofByte(0x0D), modal) == Route::commander);
    CHECK(routeKey(HostKey::ofByte(0x03), modal) == Route::commander);
    CHECK(routeKey(HostKey::ofByte(0x0F), modal) == Route::commander);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::f10), modal) == Route::commander);

    RouteState hidden = on();
    hidden.panelsHidden = true;
    CHECK(routeKey(HostKey::ofByte('a'), hidden) == Route::guest);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::f3), hidden) == Route::guest);
    CHECK(routeKey(HostKey::ofSpecial(SpecialKey::up), hidden) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x09), hidden) == Route::guest);
    CHECK(routeKey(HostKey::ofByte(0x0F), hidden) == Route::hidePanels);
    CHECK(routeKey(HostKey::ofByte(0x1C), hidden) == Route::commander);   /* never the guest's, never a way down */

    /* the toggle and the hide leave the typed flag alone */
    RouteState s = on();
    s = afterRouting(s, HostKey::ofByte('x'), Route::guest);
    s = afterRouting(s, HostKey::ofByte(0x0F), Route::hidePanels);
    CHECK(s.typedPending);
}
