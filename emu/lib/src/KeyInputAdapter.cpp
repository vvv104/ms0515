/*
 * KeyInputAdapter.cpp — see header for the architectural story.
 */

#include "ms0515/KeyInputAdapter.hpp"
#include "ms0515/Emulator.hpp"

extern "C" {
#include "ms0515/keyboard.h"
}

namespace ms0515 {

namespace {

constexpr uint8_t kSC_Shift  = 0256;
constexpr uint8_t kSC_Ruslat = 0262;
constexpr uint8_t kSC_Allup  = 0263;

} /* anonymous namespace */

bool KeyInputAdapter::isShiftImmuneInLat(ms7004_key_t k)
{
    return k == MS7004_KEY_LBRACKET     /* Ш / [ */
        || k == MS7004_KEY_RBRACKET     /* Щ / ] */
        || k == MS7004_KEY_BACKSLASH    /* Э / \ */
        || k == MS7004_KEY_CHE;         /* Ч / ¬ */
}

bool KeyInputAdapter::isLetterKey(ms7004_key_t k, bool rus_mode)
{
    /* Latin alphabet positions are letters in both modes. */
    switch (k) {
    case MS7004_KEY_A: case MS7004_KEY_B: case MS7004_KEY_C:
    case MS7004_KEY_D: case MS7004_KEY_E: case MS7004_KEY_F:
    case MS7004_KEY_G: case MS7004_KEY_H: case MS7004_KEY_I:
    case MS7004_KEY_J: case MS7004_KEY_K: case MS7004_KEY_L:
    case MS7004_KEY_M: case MS7004_KEY_N: case MS7004_KEY_O:
    case MS7004_KEY_P: case MS7004_KEY_Q: case MS7004_KEY_R:
    case MS7004_KEY_S: case MS7004_KEY_T: case MS7004_KEY_U:
    case MS7004_KEY_V: case MS7004_KEY_W: case MS7004_KEY_X:
    case MS7004_KEY_Y: case MS7004_KEY_Z:
        return true;
    /* Symbol-on-letter caps: behave as letters only in РУС mode. */
    case MS7004_KEY_LBRACKET:   /* Ш / [ */
    case MS7004_KEY_RBRACKET:   /* Щ / ] */
    case MS7004_KEY_BACKSLASH:  /* Э / \ */
    case MS7004_KEY_CHE:        /* Ч / ¬ */
    case MS7004_KEY_AT:         /* Ю / @ */
    case MS7004_KEY_HARDSIGN:   /* Ъ     */
        return rus_mode;
    default:
        return false;
    }
}

bool KeyInputAdapter::effectiveShift(ms7004_key_t key, bool shift_held) const
{
    /* DEVIATE 1: Shift-immune in ЛАТ — never apply Shift. */
    if (!ruslat_on_ && isShiftImmuneInLat(key))
        return false;
    /* DEVIATE 3: letters in either mode — Shift XOR ФКС inverts case
     * (CAPS+Shift cancels back to default).  DEVIATE 2: ФКС has no
     * effect on non-letters because we only XOR caps_on for letters. */
    if (isLetterKey(key, ruslat_on_))
        return shift_held ^ caps_on_;
    return shift_held;
}

void KeyInputAdapter::emitByte(Emulator &emu, uint8_t sc)
{
    if (sc != 0) {
        kbd_push_scancode(&emu.board().kbd, sc);
    }
}

void KeyInputAdapter::clickKey(Emulator &emu, ms7004_key_t key,
                               bool shift, bool ctrl)
{
    const uint8_t sc = ms7004_scancode(key);
    if (sc == 0) return;                    /* unmapped enum value */

    const bool want_shift = effectiveShift(key, shift);

    /* DEVIATE 4: in РУС mode, the OS's character table maps some
     * symbol scancodes to Cyrillic letters (e.g. { → Ш, } → Щ, ~ → Ч).
     * Users clicking '{' on the OSK want '{', not Ш — so for non-letter
     * caps in РУС mode we sandwich a momentary RUSLAT flip around the
     * emit.  The flip keeps OUR adapter's ruslat_on_ unchanged (the
     * user's mode intent), but the OS sees LAT for one scancode. */
    const bool flip_ruslat = ruslat_on_ && !isLetterKey(key, ruslat_on_);

    /* OSK clicks are self-contained: each call emits a complete byte
     * sequence that leaves the OS in "no modifier held" state.  Don't
     * try to "restore" the user's Shift held-state at the end — that
     * would leave the OS thinking Shift is still down and corrupt the
     * next click (e.g. CAPS+Shift+RBRACKET would emit its un-shifted
     * sequence, then a trailing Shift would make the next plain BS
     * arrive as Shift+BS). */
    if (flip_ruslat) emitByte(emu, kSC_Ruslat);
    if (ctrl)        emitByte(emu, ms7004_scancode(MS7004_KEY_CTRL));
    if (want_shift)  emitByte(emu, kSC_Shift);
    emitByte(emu, sc);
    if (want_shift || ctrl) emitByte(emu, kSC_Allup);
    if (flip_ruslat) emitByte(emu, kSC_Ruslat);
}

void KeyInputAdapter::clickRuslat(Emulator &emu)
{
    ruslat_on_ = !ruslat_on_;
    emitByte(emu, kSC_Ruslat);
}

} /* namespace ms0515 */
