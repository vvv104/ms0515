/*
 * PhysicalKeyboard.hpp — Host (PC) keyboard → MS7004 event translation.
 *
 * Translates SDL key events into ms7004 key presses/releases, handling:
 *   - Character-based symbol remapping (PC labels → MS7004 output)
 *   - CapsLock+Shift inversion (modern CapsLock behaviour)
 *   - Shift override management (add/remove virtual Shift)
 *   - Numpad /,*,+ → symbol output (bypassing PF mapping)
 *   - РУС mode special cases (backslash, underscore)
 *   - SDL auto-repeat suppression (ms7004 has its own)
 *
 * The on-screen keyboard (OSK) is handled separately and does not go
 * through this module.
 */

#ifndef MS0515_FRONTEND_PHYSICAL_KEYBOARD_HPP
#define MS0515_FRONTEND_PHYSICAL_KEYBOARD_HPP

#include <SDL.h>

#include <unordered_map>
#include <vector>

extern "C" {
#include <ms0515/ms7004.h>
}

namespace ms0515 { class Emulator; }

namespace ms0515_frontend {

class PhysicalKeyboard {
public:
    /*
     * Process a single SDL key event.  Returns true if the event was
     * consumed (caller should not process it further).
     *
     * `wantCapture` — true when ImGui wants keyboard input (debugger
     * text fields, etc.).  Modifier keys are always forwarded.
     */
    bool handleEvent(const SDL_Event &ev, ms0515::Emulator &emu,
                     bool wantCapture);

    /* Frame tick: applies any matrix presses that were deferred to
     * fire on the next frame.  See "Deferred press queue" below.
     * Must be called once per frame, BEFORE SDL event polling. */
    void tick(ms0515::Emulator &emu);

    /* Host mode: when active, physical keyboard events are NOT forwarded
     * to the emulator.  Toggled by Right Ctrl. */
    bool hostMode() const { return hostMode_; }
    void setHostMode(bool m) { hostMode_ = m; }

private:
    bool hostMode_ = false;
    /* SDL scancode → ms7004_key_t that was pressed.  Used on key-up
     * to release the correct key even if RUS/LAT mode changed. */
    std::unordered_map<int, int> physToMs7004_;

    /* Shift override tracking for character-remapped keys. */
    struct ShiftOverride {
        bool added;      /* Case 1: we pressed virtual SHIFT_L */
        bool removedL;   /* Case 2: we released SHIFT_L */
        bool removedR;   /* Case 2: we released SHIFT_R */
    };
    std::unordered_map<int, ShiftOverride> shiftOverrides_;

    /* Deferred press queue: when the host-keymap remapping needs to
     * change the MS7004 Shift state AND press a key in the same SDL
     * event, the matrix-bit changes happen at literally the same
     * instant.  The keyboard firmware then sees both changes in one
     * scan pass and emits scancodes in column-scan order — for
     * synthetic-shift cases (e.g. host `'` → MS7004 Shift+7) the key
     * (col 9) is emitted BEFORE Shift (col 12), so the OS echoes the
     * unshifted glyph.  We avoid the race by setting Shift first,
     * then deferring the target-key press to the next frame so the
     * firmware has time to emit the Shift make-code.
     *
     * Side effect: the deferred press waits one frame (≈20 ms emu
     * time).  If the user releases the host key in that window, the
     * release path runs first and the queued press fires against
     * already-released state — the keystroke is silently dropped.
     * Acceptable for normal touch-typing (key holds last 80–200 ms)
     * but very fast taps may be lost. */
    struct DeferredPress {
        int           frames_to_wait;   /* fires when reaches 0 */
        ms7004_key_t  key;
    };
    std::vector<DeferredPress> deferredPresses_;

    void handleKeyDown(SDL_Scancode phys, ms0515::Emulator &emu,
                       bool wantCapture);
    void handleKeyUp  (SDL_Scancode phys, ms0515::Emulator &emu);
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_PHYSICAL_KEYBOARD_HPP */
