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

#include <ms0515/Emulator.hpp>  /* ms0515::Key */

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

    /* Host mode: when active, physical keyboard events are NOT forwarded
     * to the emulator.  Toggled by Right Ctrl. */
    bool hostMode() const { return hostMode_; }
    void setHostMode(bool m) { hostMode_ = m; }

private:
    bool hostMode_ = false;
    /* SDL scancode → ms0515::Key that was pressed.  Used on key-up
     * to release the correct key even if RUS/LAT mode changed. */
    std::unordered_map<int, int> physToMs7004_;

    /* Shift override tracking for character-remapped keys. */
    struct ShiftOverride {
        bool added;      /* Case 1: we pressed virtual SHIFT_L */
        bool removedL;   /* Case 2: we released SHIFT_L */
        bool removedR;   /* Case 2: we released SHIFT_R */
    };
    std::unordered_map<int, ShiftOverride> shiftOverrides_;

    void handleKeyDown(SDL_Scancode phys, ms0515::Emulator &emu,
                       bool wantCapture);
    void handleKeyUp  (SDL_Scancode phys, ms0515::Emulator &emu);
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_PHYSICAL_KEYBOARD_HPP */
