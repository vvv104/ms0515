/*
 * KeyboardLayout.hpp — Frontend-agnostic MS7004 on-screen keyboard layout.
 *
 * Encodes the layout side of the on-screen keyboard widget: which caps
 * exist, their relative widths in cap-units, the label printed on each
 * cap, and the `ms0515::Key` each cap is bound to (plus per-cap flags
 * like sticky/toggle/dim/gray).  No geometry, no rendering — that
 * stays on whichever frontend (SDL+ImGui, Web, …) is consuming it.
 *
 * Usage:
 *   ms0515::KeyboardLayout layout;
 *   if (!layout.loadFromFile(path)) { ... }
 *   for (const auto &row : layout.rows())
 *       for (const auto &cap : row)
 *           drawCap(cap);   // frontend-specific
 *
 * The companion classification helpers (`isLetterKey`,
 * `isShiftImmuneSymbol`) capture mode-dependent keyboard semantics
 * shared between the on-screen keyboard's UX-convenience layer and
 * the host-keyboard translator — surfaced as free functions so any
 * frontend can reuse them without duplicating the case tables.
 */

#ifndef MS0515_KEYBOARD_LAYOUT_HPP
#define MS0515_KEYBOARD_LAYOUT_HPP

#include <ms0515/Emulator.hpp>     /* ms0515::Key */

#include <string>
#include <string_view>
#include <vector>

namespace ms0515 {

class KeyboardLayout {
public:
    /* One cap on the keyboard.  `widthUnits` is the relative width
     * (1.0 = base unit; widths are taken straight from the layout
     * file).  Frontends multiply by their own pixel scale.  `key ==
     * Key::None` means the cap has no physical-key binding (decoration,
     * unbound label). */
    struct Cap {
        std::string label;       /* "top\nbottom" — rendered as two lines */
        float       widthUnits;  /* relative width in cap units            */
        bool        drawn;       /* false = cosmetic gap (skip rendering)  */
        bool        dim;         /* drawn but inert on click               */
        bool        gray;        /* gray chassis (fn / edit / arrow cluster)*/
        Key         key;         /* physical key binding                   */
        bool        sticky;      /* modifier cap: latch on click           */
        bool        toggle;      /* toggle cap (Caps / RusLat)             */
    };

    using Row = std::vector<Cap>;

    /* Load and parse a layout file from a host filesystem path.
     * Returns true on success — i.e. the file opened, parsed, and
     * yielded at least one row.  On failure the layout is left
     * empty.  Calling this replaces any previously-loaded layout. */
    [[nodiscard]] bool loadFromFile(std::string_view path);

    /* Same parser, fed from a string in memory (test entry point and
     * for embedded asset bundles). */
    [[nodiscard]] bool loadFromString(std::string_view content);

    [[nodiscard]] const std::vector<Row> &rows() const noexcept
        { return rows_; }

    [[nodiscard]] bool loaded() const noexcept
        { return !rows_.empty(); }

    void clear() noexcept { rows_.clear(); }

private:
    std::vector<Row> rows_;
};

/* ── Mode-dependent keyboard semantics ────────────────────────────────────
 *
 * Two predicates the lib uses to drive the on-screen keyboard's
 * UX-convenience layer (sticky-shift suppression, caps-mode handling,
 * Latin/Russian symbol fallback) and the host-keyboard translator's
 * CAPS+Shift inversion.  Surfaced here so every frontend reuses the
 * same rules — duplicating them per-frontend is exactly the kind of
 * subtle drift this refactor is meant to prevent. */

/* True if `k` should be treated as a letter for Caps/Shift purposes.
 * In ЛАТ mode only the 26 dual-Latin letters qualify; in РУС mode the
 * symbol-on-letter caps (Ш/[ Щ/] Э/\ Ч/¬ Ю/@ Ъ) join them. */
[[nodiscard]] bool isLetterKey(Key k, bool rusMode) noexcept;

/* True if pressing `k` while ВР (Shift) is held should skip the
 * shifted symbol and emit the base glyph instead.  Applies only in
 * ЛАТ mode to the symbol-on-letter caps (their shifted variants are
 * the four bracket/pipe glyphs the original layout never shipped with
 * those caps). */
[[nodiscard]] bool isShiftImmuneSymbol(Key k, bool rusMode) noexcept;

} /* namespace ms0515 */

#endif /* MS0515_KEYBOARD_LAYOUT_HPP */
