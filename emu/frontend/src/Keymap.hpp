/*
 * Keymap.hpp — SDL physical scancode → MS7004 key mapping.
 *
 * Translates host keyboard events (SDL scancodes) into ms7004_key_t
 * physical key identifiers.  The ms7004 microcontroller model in core
 * handles all protocol details (modifier latching, ALL-UP, toggles,
 * auto-repeat); this module is a pure lookup table with no state.
 *
 * In Latin mode: letter keys use character-based mapping (host A → MS7004
 * key A), so QWERTY users get the expected Latin letters.  In Russian
 * mode: letter keys follow ЙЦУКЕН positional layout (host Q → MS7004
 * key J/Й, matching the Russian letter at that physical position on
 * the real MS7004 keyboard).
 */

#ifndef MS0515_FRONTEND_KEYMAP_HPP
#define MS0515_FRONTEND_KEYMAP_HPP

#include <SDL2/SDL_scancode.h>

extern "C" {
#include <ms0515/ms7004.h>
}

namespace ms0515_frontend {

/*
 * Map an SDL physical scancode to an MS7004 key.
 *
 *   phys    — SDL physical scancode (ev.key.keysym.scancode)
 *   rusMode — true when the emulated machine is in РУС mode
 *
 * Returns MS7004_KEY_NONE for unmapped scancodes.
 */
ms7004_key_t sdlToMs7004(SDL_Scancode phys, bool rusMode);

/*
 * Character-based mapping result.  For symbol keys in ЛАТ mode the
 * target MS7004 key and/or Shift state may differ from the positional
 * mapping so that the emulated output matches the PC keyboard label.
 */
struct Ms7004Mapped {
    ms7004_key_t key;
    bool withShift;   /* true = this key needs Shift on MS7004 to
                       * produce the intended character */
};

/*
 * Character-based mapping: (SDL scancode, host Shift state, RUS mode)
 * → (MS7004 key, MS7004 Shift needed).
 *
 * In РУС mode the existing positional mapping is used.  In ЛАТ mode,
 * symbol keys are remapped so that a US-layout PC keyboard produces
 * the same characters as its key labels, not the MS7004 shifted layout.
 */
Ms7004Mapped sdlToMs7004Char(SDL_Scancode phys, bool shifted, bool rusMode);

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_KEYMAP_HPP */
