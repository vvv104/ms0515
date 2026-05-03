/*
 * PhysicalKeyboard.cpp — Host (PC) keyboard → MS7004 event translation.
 *
 * See PhysicalKeyboard.hpp for the interface contract.
 */

#include "PhysicalKeyboard.hpp"
#include "Keymap.hpp"

#include <ms0515/Emulator.hpp>

namespace ms0515_frontend {

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Letter key classification for CapsLock+Shift inversion (DEVIATE 3).
 * Same logic as isLetterKey() in OnScreenKeyboard.cpp. */
static bool isPhysLetterKey(ms0515::Key k, bool rusMode)
{
    switch (k) {
    case ms0515::Key::A: case ms0515::Key::B: case ms0515::Key::C:
    case ms0515::Key::D: case ms0515::Key::E: case ms0515::Key::F:
    case ms0515::Key::G: case ms0515::Key::H: case ms0515::Key::I:
    case ms0515::Key::J: case ms0515::Key::K: case ms0515::Key::L:
    case ms0515::Key::M: case ms0515::Key::N: case ms0515::Key::O:
    case ms0515::Key::P: case ms0515::Key::Q: case ms0515::Key::R:
    case ms0515::Key::S: case ms0515::Key::T: case ms0515::Key::U:
    case ms0515::Key::V: case ms0515::Key::W: case ms0515::Key::X:
    case ms0515::Key::Y: case ms0515::Key::Z:
        return true;
    case ms0515::Key::LBracket: case ms0515::Key::RBracket:
    case ms0515::Key::Backslash: case ms0515::Key::Che:
    case ms0515::Key::At: case ms0515::Key::HardSign:
        return rusMode;
    default:
        return false;
    }
}

/* ── Public interface ────────────────────────────────────────────────── */

bool PhysicalKeyboard::handleEvent(const SDL_Event &ev,
                                   ms0515::Emulator &emu,
                                   bool wantCapture)
{
    if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP)
        return false;

    SDL_Scancode phys = ev.key.keysym.scancode;

    /* Right Ctrl toggles host mode (keyboard disconnected from emulator).
     * When host mode is active, all other keys are suppressed. */
    if (phys == SDL_SCANCODE_RCTRL) {
        if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            hostMode_ = !hostMode_;
            if (hostMode_)
                emu.keyReleaseAll();
        }
        return true;
    }

    if (hostMode_)
        return true;   /* swallow everything while in host mode */

    if (ev.type == SDL_KEYDOWN) {
        if (ev.key.repeat)
            return true;   /* suppress SDL auto-repeat */
        handleKeyDown(phys, emu, wantCapture);
    } else {
        handleKeyUp(phys, emu);
    }
    return true;
}

/* ── Key-down ────────────────────────────────────────────────────────── */

void PhysicalKeyboard::handleKeyDown(SDL_Scancode phys,
                                     ms0515::Emulator &emu,
                                     bool wantCapture)
{
    /* Numpad /,*,+: map to the corresponding symbol key with proper
     * down/up tracking so auto-repeat works.  No OSK highlight (no
     * physToMs7004 entry).  PF2-4 remain OSK-only. */
    if (phys == SDL_SCANCODE_KP_DIVIDE) {
        emu.keyPress(ms0515::Key::Slash, true);
        return;
    }
    if (phys == SDL_SCANCODE_KP_MULTIPLY) {
        emu.keyPress(ms0515::Key::ShiftL, true);
        emu.keyPress(ms0515::Key::ColonStar, true);
        return;
    }
    if (phys == SDL_SCANCODE_KP_PLUS) {
        emu.keyPress(ms0515::Key::ShiftL, true);
        emu.keyPress(ms0515::Key::SemiPlus, true);
        return;
    }

    /* РУС mode: PC backslash (unshifted) → \ character.
     * BACKSLASH scancode is a letter key in РУС (Э), so we must
     * temporarily switch to ЛАТ.  Instant press+release. */
    if (emu.ruslatOn() &&
        !(SDL_GetModState() & KMOD_SHIFT) &&
        phys == SDL_SCANCODE_BACKSLASH) {
        emu.keyPress(ms0515::Key::RusLat, true);
        emu.keyPress(ms0515::Key::RusLat, false);
        emu.keyPress(ms0515::Key::Backslash, true);
        emu.keyPress(ms0515::Key::Backslash, false);
        emu.keyPress(ms0515::Key::RusLat, true);
        emu.keyPress(ms0515::Key::RusLat, false);
        return;
    }
    /* РУС mode: Shift+minus → _ (UNDERSCORE scancode = Ъ in РУС). */
    if (emu.ruslatOn() &&
        (SDL_GetModState() & KMOD_SHIFT) &&
        phys == SDL_SCANCODE_MINUS) {
        emu.keyPress(ms0515::Key::RusLat, true);
        emu.keyPress(ms0515::Key::RusLat, false);
        emu.keyPress(ms0515::Key::Underscore, true);
        emu.keyPress(ms0515::Key::Underscore, false);
        emu.keyPress(ms0515::Key::RusLat, true);
        emu.keyPress(ms0515::Key::RusLat, false);
        return;
    }

    const bool rusMode   = emu.ruslatOn();
    const bool shiftL    = emu.keyHeld(ms0515::Key::ShiftL);
    const bool shiftR    = emu.keyHeld(ms0515::Key::ShiftR);
    const bool hostShift = shiftL || shiftR;

    /* Character-based mapping: resolves the target MS7004 key AND
     * whether Shift should be active on the MS7004 side. */
    auto mapped = sdlToMs7004Char(phys, hostShift, rusMode);
    ms0515::Key key = mapped.key;

    if (key == ms0515::Key::None)
        return;

    physToMs7004_[(int)phys] = (int)key;

    /* Let modifiers through even when ImGui wants keyboard. */
    if (wantCapture &&
        key != ms0515::Key::ShiftL &&
        key != ms0515::Key::ShiftR &&
        key != ms0515::Key::Ctrl    &&
        key != ms0515::Key::Caps    &&
        key != ms0515::Key::Compose &&
        key != ms0515::Key::RusLat)
        return;

    /* [DEVIATE 3] CapsLock+Shift inversion: when CAPS is on and
     * Shift is physically held, letter keys produce lowercase. */
    const bool capsInvert =
        isPhysLetterKey(key, rusMode) && emu.capsOn() && hostShift;

    /* Does the MS7004 Shift state need to differ from host? */
    const bool needShift   = capsInvert ? false : mapped.withShift;
    const bool shiftChange = needShift != hostShift;

    if (capsInvert) {
        /* CapsLock+Shift inversion: synthetic press+release. */
        if (shiftL) emu.keyPress(ms0515::Key::ShiftL, false);
        if (shiftR) emu.keyPress(ms0515::Key::ShiftR, false);
        emu.keyPress(ms0515::Key::Caps, true);
        emu.keyPress(ms0515::Key::Caps, false);
        emu.keyPress(key, true);
        emu.keyPress(key, false);
        emu.keyPress(ms0515::Key::Caps, true);
        emu.keyPress(ms0515::Key::Caps, false);
        if (shiftL) emu.keyPress(ms0515::Key::ShiftL, true);
        if (shiftR) emu.keyPress(ms0515::Key::ShiftR, true);
    } else if (shiftChange) {
        /* Character remapping changed the Shift requirement.
         * Keep the key HELD so the OSK highlights correctly. */
        if (hostShift && !needShift) {
            /* Case 2: remove Shift, hold key. */
            if (shiftL) emu.keyPress(ms0515::Key::ShiftL, false);
            if (shiftR) emu.keyPress(ms0515::Key::ShiftR, false);
            emu.keyPress(key, true);
            shiftOverrides_[(int)phys] = {false, shiftL, shiftR};
        } else {
            /* Case 1: add Shift, hold key. */
            emu.keyPress(ms0515::Key::ShiftL, true);
            emu.keyPress(key, true);
            shiftOverrides_[(int)phys] = {true, false, false};
        }
    } else {
        emu.keyPress(key, true);
    }
}

/* ── Key-up ──────────────────────────────────────────────────────────── */

void PhysicalKeyboard::handleKeyUp(SDL_Scancode phys,
                                   ms0515::Emulator &emu)
{
    /* Numpad /,*,+: release the mapped symbol key. */
    if (phys == SDL_SCANCODE_KP_DIVIDE) {
        emu.keyPress(ms0515::Key::Slash, false);
        return;
    }
    if (phys == SDL_SCANCODE_KP_MULTIPLY) {
        emu.keyPress(ms0515::Key::ColonStar, false);
        emu.keyPress(ms0515::Key::ShiftL, false);
        return;
    }
    if (phys == SDL_SCANCODE_KP_PLUS) {
        emu.keyPress(ms0515::Key::SemiPlus, false);
        emu.keyPress(ms0515::Key::ShiftL, false);
        return;
    }

    /* Release the same ms7004 key that was pressed, even if the
     * RUS/LAT mode changed in between. */
    auto it = physToMs7004_.find((int)phys);
    if (it == physToMs7004_.end())
        return;

    emu.keyPress(static_cast<ms0515::Key>(it->second), false);

    /* Undo any Shift override from char remapping. */
    auto ov = shiftOverrides_.find((int)phys);
    if (ov != shiftOverrides_.end()) {
        if (ov->second.added) {
            /* Case 1: remove virtual Shift — but only if no physical
             * Shift is actually held. */
            bool physShift =
                physToMs7004_.count((int)SDL_SCANCODE_LSHIFT) ||
                physToMs7004_.count((int)SDL_SCANCODE_RSHIFT);
            if (!physShift)
                emu.keyPress(ms0515::Key::ShiftL, false);
        }
        if (ov->second.removedL) {
            /* Case 2: restore left Shift if still physically held. */
            if (physToMs7004_.count((int)SDL_SCANCODE_LSHIFT))
                emu.keyPress(ms0515::Key::ShiftL, true);
        }
        if (ov->second.removedR) {
            if (physToMs7004_.count((int)SDL_SCANCODE_RSHIFT))
                emu.keyPress(ms0515::Key::ShiftR, true);
        }
        shiftOverrides_.erase(ov);
    }

    physToMs7004_.erase(it);
}

} /* namespace ms0515_frontend */
