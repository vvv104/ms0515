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
static bool isPhysLetterKey(ms7004_key_t k, bool rusMode)
{
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
    case MS7004_KEY_LBRACKET: case MS7004_KEY_RBRACKET:
    case MS7004_KEY_BACKSLASH: case MS7004_KEY_CHE:
    case MS7004_KEY_AT: case MS7004_KEY_HARDSIGN:
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
        emu.keyPress(MS7004_KEY_SLASH, true);
        return;
    }
    if (phys == SDL_SCANCODE_KP_MULTIPLY) {
        emu.keyPress(MS7004_KEY_SHIFT_L, true);
        emu.keyPress(MS7004_KEY_COLON_STAR, true);
        return;
    }
    if (phys == SDL_SCANCODE_KP_PLUS) {
        emu.keyPress(MS7004_KEY_SHIFT_L, true);
        emu.keyPress(MS7004_KEY_SEMI_PLUS, true);
        return;
    }

    /* РУС mode: PC backslash (unshifted) → \ character.
     * BACKSLASH scancode is a letter key in РУС (Э), so we must
     * temporarily switch to ЛАТ.  Instant press+release. */
    if (emu.ruslatOn() &&
        !(SDL_GetModState() & KMOD_SHIFT) &&
        phys == SDL_SCANCODE_BACKSLASH) {
        emu.keyPress(MS7004_KEY_RUSLAT, true);
        emu.keyPress(MS7004_KEY_RUSLAT, false);
        emu.keyPress(MS7004_KEY_BACKSLASH, true);
        emu.keyPress(MS7004_KEY_BACKSLASH, false);
        emu.keyPress(MS7004_KEY_RUSLAT, true);
        emu.keyPress(MS7004_KEY_RUSLAT, false);
        return;
    }
    /* РУС mode: Shift+minus → _ (UNDERSCORE scancode = Ъ in РУС). */
    if (emu.ruslatOn() &&
        (SDL_GetModState() & KMOD_SHIFT) &&
        phys == SDL_SCANCODE_MINUS) {
        emu.keyPress(MS7004_KEY_RUSLAT, true);
        emu.keyPress(MS7004_KEY_RUSLAT, false);
        emu.keyPress(MS7004_KEY_UNDERSCORE, true);
        emu.keyPress(MS7004_KEY_UNDERSCORE, false);
        emu.keyPress(MS7004_KEY_RUSLAT, true);
        emu.keyPress(MS7004_KEY_RUSLAT, false);
        return;
    }

    const bool rusMode   = emu.ruslatOn();
    const bool shiftL    = emu.keyHeld(MS7004_KEY_SHIFT_L);
    const bool shiftR    = emu.keyHeld(MS7004_KEY_SHIFT_R);
    const bool hostShift = shiftL || shiftR;

    /* Character-based mapping: resolves the target MS7004 key AND
     * whether Shift should be active on the MS7004 side. */
    auto mapped = sdlToMs7004Char(phys, hostShift, rusMode);
    ms7004_key_t key = mapped.key;

    if (key == MS7004_KEY_NONE)
        return;

    /* Toggle keys (ФКС, РУС/ЛАТ): route through the input adapter so
     * caps_on / ruslat_on stay in sync with the OSK display.  No
     * SDL_KEYUP handling needed — toggles are one-shot. */
    if (key == MS7004_KEY_CAPS) {
        emu.inputAdapter().clickCaps();
        return;
    }
    if (key == MS7004_KEY_RUSLAT) {
        emu.inputAdapter().clickRuslat(emu);
        return;
    }

    physToMs7004_[(int)phys] = (int)key;

    /* Let modifiers through even when ImGui wants keyboard. */
    if (wantCapture &&
        key != MS7004_KEY_SHIFT_L &&
        key != MS7004_KEY_SHIFT_R &&
        key != MS7004_KEY_CTRL    &&
        key != MS7004_KEY_CAPS    &&
        key != MS7004_KEY_COMPOSE &&
        key != MS7004_KEY_RUSLAT)
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
        if (shiftL) emu.keyPress(MS7004_KEY_SHIFT_L, false);
        if (shiftR) emu.keyPress(MS7004_KEY_SHIFT_R, false);
        emu.keyPress(MS7004_KEY_CAPS, true);
        emu.keyPress(MS7004_KEY_CAPS, false);
        emu.keyPress(key, true);
        emu.keyPress(key, false);
        emu.keyPress(MS7004_KEY_CAPS, true);
        emu.keyPress(MS7004_KEY_CAPS, false);
        if (shiftL) emu.keyPress(MS7004_KEY_SHIFT_L, true);
        if (shiftR) emu.keyPress(MS7004_KEY_SHIFT_R, true);
    } else if (shiftChange) {
        /* Character remapping changed the Shift requirement.  Defer
         * the target-key press by one frame so the keyboard firmware
         * has time to emit the Shift state-change scancode BEFORE the
         * key make-code, avoiding the column-scan race where col(key)
         * < col(SHIFT_L) lets the firmware emit the key first. */
        if (hostShift && !needShift) {
            /* Case 2: remove Shift now, defer key press. */
            if (shiftL) emu.keyPress(MS7004_KEY_SHIFT_L, false);
            if (shiftR) emu.keyPress(MS7004_KEY_SHIFT_R, false);
            deferredPresses_.push_back({0, key});
            shiftOverrides_[(int)phys] = {false, shiftL, shiftR};
        } else {
            /* Case 1: add Shift now, defer key press. */
            emu.keyPress(MS7004_KEY_SHIFT_L, true);
            deferredPresses_.push_back({0, key});
            shiftOverrides_[(int)phys] = {true, false, false};
        }
    } else {
        emu.keyPress(key, true);
    }
}

/* ── Per-frame tick ──────────────────────────────────────────────────── */

void PhysicalKeyboard::tick(ms0515::Emulator &emu)
{
    /* Process deferred presses queued by handleKeyDown.  Called once
     * per frame BEFORE SDL polling, so a press queued at the end of
     * frame N fires at the start of frame N+1 — one frame after the
     * Shift state-change had a chance to scan + bit-bang on the wire. */
    for (auto it = deferredPresses_.begin(); it != deferredPresses_.end(); ) {
        if (it->frames_to_wait <= 0) {
            emu.keyPress(it->key, true);
            it = deferredPresses_.erase(it);
        } else {
            --it->frames_to_wait;
            ++it;
        }
    }
}

/* ── Key-up ──────────────────────────────────────────────────────────── */

void PhysicalKeyboard::handleKeyUp(SDL_Scancode phys,
                                   ms0515::Emulator &emu)
{
    /* Numpad /,*,+: release the mapped symbol key. */
    if (phys == SDL_SCANCODE_KP_DIVIDE) {
        emu.keyPress(MS7004_KEY_SLASH, false);
        return;
    }
    if (phys == SDL_SCANCODE_KP_MULTIPLY) {
        emu.keyPress(MS7004_KEY_COLON_STAR, false);
        emu.keyPress(MS7004_KEY_SHIFT_L, false);
        return;
    }
    if (phys == SDL_SCANCODE_KP_PLUS) {
        emu.keyPress(MS7004_KEY_SEMI_PLUS, false);
        emu.keyPress(MS7004_KEY_SHIFT_L, false);
        return;
    }

    /* Release the same ms7004 key that was pressed, even if the
     * RUS/LAT mode changed in between. */
    auto it = physToMs7004_.find((int)phys);
    if (it == physToMs7004_.end())
        return;

    emu.keyPress(static_cast<ms7004_key_t>(it->second), false);

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
                emu.keyPress(MS7004_KEY_SHIFT_L, false);
        }
        if (ov->second.removedL) {
            /* Case 2: restore left Shift if still physically held. */
            if (physToMs7004_.count((int)SDL_SCANCODE_LSHIFT))
                emu.keyPress(MS7004_KEY_SHIFT_L, true);
        }
        if (ov->second.removedR) {
            if (physToMs7004_.count((int)SDL_SCANCODE_RSHIFT))
                emu.keyPress(MS7004_KEY_SHIFT_R, true);
        }
        shiftOverrides_.erase(ov);
    }

    physToMs7004_.erase(it);
}

} /* namespace ms0515_frontend */
