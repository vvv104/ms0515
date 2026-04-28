/*
 * KeyInputAdapter.hpp — UX-friendly OSK / virtual-keyboard input layer.
 *
 * The C core (ms7004.c) emulates the real keyboard firmware faithfully:
 * pressing a cap on the matrix is exactly what the hardware sees, and
 * the firmware emits authentic LK201 scancodes back to the host UART.
 * Some real-hardware quirks are inconvenient for users typing into an
 * emulated machine via mouse-clicks on an on-screen keyboard:
 *
 *   [DEVIATE 1] Shift on shift-immune keys (Ш/[, Щ/], Э/\, Ч/¬) in
 *     ЛАТ mode.  The MS7004 ROM maps Shift+Ш to '{', etc., echoing
 *     IBM-PC pairings the keycap was never designed for.  Users who
 *     click '[' on the OSK want '['; if they Shift+click they still
 *     want '[' (the cap has only one Latin glyph).
 *
 *   [DEVIATE 2] ФКС (CapsLock) on non-letter keys.  Real hardware
 *     applies CAPS to the whole keymap, scrambling digits and symbols.
 *     OSK users expect CAPS to invert *letters only*.
 *
 *   [DEVIATE 3] ФКС + ВР (Shift) on letters.  Real hardware: result
 *     is uppercase regardless (Shift and CAPS do not cancel).  Modern
 *     users expect CAPS+Shift to cancel back to lowercase default.
 *
 *   [DEVIATE 4] РУС/ЛАТ + non-letter keys.  Real hardware maps some
 *     symbol scancodes to Cyrillic letters in РУС mode (e.g. { → Ш,
 *     } → Щ, ~ → Ч).  OSK users expect non-letter caps to keep their
 *     ЛАТ symbol regardless of the РУС/ЛАТ mode.
 *
 * This adapter sits in the C++ lib layer above the C core and applies
 * those four overrides when synthesising a click.  Physical keyboard
 * events (SDL key events through PhysicalKeyboard.cpp) bypass the
 * adapter and go straight to ms7004_key — they get authentic firmware
 * behaviour, matching what a real keyboard plugged into an MS0515
 * would do.
 *
 * Implementation note: clicks bypass the firmware entirely.  The
 * adapter computes the byte sequence the firmware would have emitted
 * (with the deviations applied) and pushes it directly into the
 * USART's RX FIFO via kbd_push_scancode.  The matrix is not touched.
 */

#ifndef MS0515_KEY_INPUT_ADAPTER_HPP
#define MS0515_KEY_INPUT_ADAPTER_HPP

extern "C" {
#include "ms0515/ms7004.h"
}

namespace ms0515 {

class Emulator;

class KeyInputAdapter {
public:
    KeyInputAdapter() = default;

    /* Synthesise an OSK click: applies DEVIATE 1..4 overrides and
     * pushes the resulting scancode sequence into the host's USART
     * RX FIFO.  `shift` and `ctrl` reflect modifier buttons held by
     * the user at click time. */
    void clickKey(Emulator &emu, ms7004_key_t key,
                  bool shift = false, bool ctrl = false);

    /* OSK CAPS button: toggles our virtual ФКС state.  Does NOT emit
     * 0o260 to the OS — like the old hand-rolled state machine, the
     * adapter applies CAPS locally via case mapping rather than
     * relying on the OS to honour the scancode. */
    void clickCaps()        { caps_on_ = !caps_on_; }

    /* OSK РУС/ЛАТ button: toggles our virtual mode AND emits 0o262
     * to the OS.  The OS *does* honour the RUSLAT scancode, so we
     * forward it; we also track the state locally to drive DEVIATE 1
     * and DEVIATE 4. */
    void clickRuslat(Emulator &emu);

    [[nodiscard]] bool capsOn()   const noexcept { return caps_on_;   }
    [[nodiscard]] bool ruslatOn() const noexcept { return ruslat_on_; }

    /* Restore the adapter's view of the toggle state from a snapshot
     * load.  Does NOT emit any scancode — the C-core's matching
     * fields were already restored by the snapshot reader. */
    void setState(bool caps, bool ruslat) noexcept
    {
        caps_on_   = caps;
        ruslat_on_ = ruslat;
    }

    /* Reset the adapter to power-on defaults (used by Emulator::reset
     * to keep the OSK display in sync with the model). */
    void reset()            { caps_on_ = false; ruslat_on_ = false; }

private:
    bool caps_on_   = false;
    bool ruslat_on_ = false;

    static bool isShiftImmuneInLat(ms7004_key_t k);
    static bool isLetterKey(ms7004_key_t k, bool rus_mode);
    bool effectiveShift(ms7004_key_t key, bool shift_held) const;
    static void emitByte(Emulator &emu, uint8_t sc);
};

} /* namespace ms0515 */

#endif /* MS0515_KEY_INPUT_ADAPTER_HPP */
