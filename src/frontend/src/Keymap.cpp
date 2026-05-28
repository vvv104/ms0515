/*
 * Keymap.cpp — SDL physical scancode → MS7004 key mapping.
 *
 * Pure lookup, no state.  See Keymap.hpp for the design overview.
 */

#include "Keymap.hpp"

namespace ms0515_frontend {

ms0515::Key sdlToMs7004(SDL_Scancode phys, bool rusMode)
{
    /* ── Mode-dependent letter and symbol mappings ──────────────────── */

    if (rusMode) {
        /* ЙЦУКЕН positional: host physical position → MS7004 key for the
         * Russian letter at that position on the real keyboard. */
        switch (phys) {
        /* Top letter row: Й Ц У К Е Н Г Ш Щ З Х Ъ */
        case SDL_SCANCODE_Q:            return ms0515::Key::J;
        case SDL_SCANCODE_W:            return ms0515::Key::C;
        case SDL_SCANCODE_E:            return ms0515::Key::U;
        case SDL_SCANCODE_R:            return ms0515::Key::K;
        case SDL_SCANCODE_T:            return ms0515::Key::E;
        case SDL_SCANCODE_Y:            return ms0515::Key::N;
        case SDL_SCANCODE_U:            return ms0515::Key::G;
        case SDL_SCANCODE_I:            return ms0515::Key::LBracket;
        case SDL_SCANCODE_O:            return ms0515::Key::RBracket;
        case SDL_SCANCODE_P:            return ms0515::Key::Z;
        case SDL_SCANCODE_LEFTBRACKET:  return ms0515::Key::H;
        case SDL_SCANCODE_RIGHTBRACKET: return ms0515::Key::HardSign;

        /* Home row: Ф Ы В А П Р О Л Д Ж Э */
        case SDL_SCANCODE_A:            return ms0515::Key::F;
        case SDL_SCANCODE_S:            return ms0515::Key::Y;
        case SDL_SCANCODE_D:            return ms0515::Key::W;
        case SDL_SCANCODE_F:            return ms0515::Key::A;
        case SDL_SCANCODE_G:            return ms0515::Key::P;
        case SDL_SCANCODE_H:            return ms0515::Key::R;
        case SDL_SCANCODE_J:            return ms0515::Key::O;
        case SDL_SCANCODE_K:            return ms0515::Key::L;
        case SDL_SCANCODE_L:            return ms0515::Key::D;
        case SDL_SCANCODE_SEMICOLON:    return ms0515::Key::V;
        case SDL_SCANCODE_APOSTROPHE:   return ms0515::Key::Backslash;

        /* Bottom row: Я Ч С М И Т Ь Б Ю */
        case SDL_SCANCODE_Z:            return ms0515::Key::Q;
        case SDL_SCANCODE_X:            return ms0515::Key::Che;
        case SDL_SCANCODE_C:            return ms0515::Key::S;
        case SDL_SCANCODE_V:            return ms0515::Key::M;
        case SDL_SCANCODE_B:            return ms0515::Key::I;
        case SDL_SCANCODE_N:            return ms0515::Key::T;
        case SDL_SCANCODE_M:            return ms0515::Key::X;
        case SDL_SCANCODE_COMMA:        return ms0515::Key::B;
        case SDL_SCANCODE_PERIOD:       return ms0515::Key::At;
        default: break; /* fall through to common */
        }
    } else {
        /* Latin character-based: host letter → MS7004 key with the
         * matching Latin character, regardless of physical position. */
        switch (phys) {
        case SDL_SCANCODE_A: return ms0515::Key::A;
        case SDL_SCANCODE_B: return ms0515::Key::B;
        case SDL_SCANCODE_C: return ms0515::Key::C;
        case SDL_SCANCODE_D: return ms0515::Key::D;
        case SDL_SCANCODE_E: return ms0515::Key::E;
        case SDL_SCANCODE_F: return ms0515::Key::F;
        case SDL_SCANCODE_G: return ms0515::Key::G;
        case SDL_SCANCODE_H: return ms0515::Key::H;
        case SDL_SCANCODE_I: return ms0515::Key::I;
        case SDL_SCANCODE_J: return ms0515::Key::J;
        case SDL_SCANCODE_K: return ms0515::Key::K;
        case SDL_SCANCODE_L: return ms0515::Key::L;
        case SDL_SCANCODE_M: return ms0515::Key::M;
        case SDL_SCANCODE_N: return ms0515::Key::N;
        case SDL_SCANCODE_O: return ms0515::Key::O;
        case SDL_SCANCODE_P: return ms0515::Key::P;
        case SDL_SCANCODE_Q: return ms0515::Key::Q;
        case SDL_SCANCODE_R: return ms0515::Key::R;
        case SDL_SCANCODE_S: return ms0515::Key::S;
        case SDL_SCANCODE_T: return ms0515::Key::T;
        case SDL_SCANCODE_U: return ms0515::Key::U;
        case SDL_SCANCODE_V: return ms0515::Key::V;
        case SDL_SCANCODE_W: return ms0515::Key::W;
        case SDL_SCANCODE_X: return ms0515::Key::X;
        case SDL_SCANCODE_Y: return ms0515::Key::Y;
        case SDL_SCANCODE_Z: return ms0515::Key::Z;

        /* Symbol keys in Latin mode */
        case SDL_SCANCODE_LEFTBRACKET:  return ms0515::Key::LBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return ms0515::Key::RBracket;
        case SDL_SCANCODE_SEMICOLON:    return ms0515::Key::SemiPlus;
        case SDL_SCANCODE_APOSTROPHE:   return ms0515::Key::Tilde;
        case SDL_SCANCODE_COMMA:        return ms0515::Key::Comma;
        case SDL_SCANCODE_PERIOD:       return ms0515::Key::Period;
        default: break; /* fall through to common */
        }
    }

    /* ── Mode-independent mappings ─────────────────────────────────── */
    switch (phys) {

    /* Modifiers */
    case SDL_SCANCODE_LSHIFT:   return ms0515::Key::ShiftL;
    case SDL_SCANCODE_RSHIFT:   return ms0515::Key::ShiftR;
    case SDL_SCANCODE_LCTRL:    return ms0515::Key::Ctrl;
    /* RCTRL is reserved as the host-mode toggle key (PhysicalKeyboard) */
    case SDL_SCANCODE_CAPSLOCK: return ms0515::Key::Caps;
    case SDL_SCANCODE_LALT:     return ms0515::Key::Compose;
    case SDL_SCANCODE_RALT:     return ms0515::Key::RusLat;

    /* Toggle */
    /* (no host key for РУС/ЛАТ — only via OSK or a custom binding) */

    /* Digits */
    case SDL_SCANCODE_1: return ms0515::Key::Digit1;
    case SDL_SCANCODE_2: return ms0515::Key::Digit2;
    case SDL_SCANCODE_3: return ms0515::Key::Digit3;
    case SDL_SCANCODE_4: return ms0515::Key::Digit4;
    case SDL_SCANCODE_5: return ms0515::Key::Digit5;
    case SDL_SCANCODE_6: return ms0515::Key::Digit6;
    case SDL_SCANCODE_7: return ms0515::Key::Digit7;
    case SDL_SCANCODE_8: return ms0515::Key::Digit8;
    case SDL_SCANCODE_9: return ms0515::Key::Digit9;
    case SDL_SCANCODE_0: return ms0515::Key::Digit0;

    /* Digit-row symbols (same in both modes) */
    case SDL_SCANCODE_GRAVE:     return ms0515::Key::LBracePipe;
    case SDL_SCANCODE_MINUS:     return ms0515::Key::MinusEq;
    case SDL_SCANCODE_EQUALS:    return ms0515::Key::RBraceLeftUp;
    case SDL_SCANCODE_BACKSLASH: return ms0515::Key::Backslash;
    case SDL_SCANCODE_SLASH:     return ms0515::Key::Slash;

    /* Whitespace / editing */
    case SDL_SCANCODE_SPACE:     return ms0515::Key::Space;
    case SDL_SCANCODE_RETURN:    return ms0515::Key::Return;
    case SDL_SCANCODE_TAB:       return ms0515::Key::Tab;
    case SDL_SCANCODE_BACKSPACE: return ms0515::Key::Backspace;

    /* Editing cluster */
    case SDL_SCANCODE_HOME:      return ms0515::Key::Find;
    case SDL_SCANCODE_INSERT:    return ms0515::Key::Insert;
    case SDL_SCANCODE_DELETE:    return ms0515::Key::Remove;
    case SDL_SCANCODE_END:       return ms0515::Key::Select;
    case SDL_SCANCODE_PAGEUP:    return ms0515::Key::Prev;
    case SDL_SCANCODE_PAGEDOWN:  return ms0515::Key::Next;

    /* Arrows */
    case SDL_SCANCODE_LEFT:  return ms0515::Key::Left;
    case SDL_SCANCODE_RIGHT: return ms0515::Key::Right;
    case SDL_SCANCODE_UP:    return ms0515::Key::Up;
    case SDL_SCANCODE_DOWN:  return ms0515::Key::Down;

    /* Function keys.
     * Note: SDL_SCANCODE_ESCAPE intentionally NOT mapped — host ESC is
     * reserved for the frontend to exit fullscreen (see main.cpp event
     * loop).  The guest's Ф11 is reachable via the host F11 key. */
    case SDL_SCANCODE_F1:    return ms0515::Key::F1;
    case SDL_SCANCODE_F2:    return ms0515::Key::F2;
    case SDL_SCANCODE_F3:    return ms0515::Key::F3;
    case SDL_SCANCODE_F4:    return ms0515::Key::F4;
    case SDL_SCANCODE_F5:    return ms0515::Key::F5;
    case SDL_SCANCODE_F6:    return ms0515::Key::F6;
    case SDL_SCANCODE_F7:    return ms0515::Key::F7;
    case SDL_SCANCODE_F8:    return ms0515::Key::F8;
    case SDL_SCANCODE_F9:    return ms0515::Key::F9;
    case SDL_SCANCODE_F10:   return ms0515::Key::F10;
    case SDL_SCANCODE_F11:   return ms0515::Key::F11;
    case SDL_SCANCODE_F12:   return ms0515::Key::F12;
    case SDL_SCANCODE_F13:   return ms0515::Key::F13;

    /* PF keys (top of numpad on modern keyboards).
     * KP_DIVIDE, KP_MULTIPLY, KP_PLUS are handled in main.cpp as
     * symbol keys (/, *, +); PF2-4 are OSK-only. */
    case SDL_SCANCODE_NUMLOCKCLEAR: return ms0515::Key::Pf1;

    /* Numpad */
    case SDL_SCANCODE_KP_0:      return ms0515::Key::Kp0Wide;
    case SDL_SCANCODE_KP_1:      return ms0515::Key::Kp1;
    case SDL_SCANCODE_KP_2:      return ms0515::Key::Kp2;
    case SDL_SCANCODE_KP_3:      return ms0515::Key::Kp3;
    case SDL_SCANCODE_KP_4:      return ms0515::Key::Kp4;
    case SDL_SCANCODE_KP_5:      return ms0515::Key::Kp5;
    case SDL_SCANCODE_KP_6:      return ms0515::Key::Kp6;
    case SDL_SCANCODE_KP_7:      return ms0515::Key::Kp7;
    case SDL_SCANCODE_KP_8:      return ms0515::Key::Kp8;
    case SDL_SCANCODE_KP_9:      return ms0515::Key::Kp9;
    case SDL_SCANCODE_KP_PERIOD: return ms0515::Key::KpDot;
    case SDL_SCANCODE_KP_ENTER:  return ms0515::Key::KpEnter;
    case SDL_SCANCODE_KP_MINUS:  return ms0515::Key::KpMinus;
    case SDL_SCANCODE_KP_COMMA:  return ms0515::Key::KpComma;

    default: return ms0515::Key::None;
    }
}

Ms7004Mapped sdlToMs7004Char(SDL_Scancode phys, bool shifted, bool rusMode)
{
    ms0515::Key base = sdlToMs7004(phys, rusMode);

    /* ── РУС mode: Russian PC keyboard symbol remapping ──────────────────
     *
     * Letters use positional ЙЦУКЕН (handled by sdlToMs7004).  Symbol
     * keys are remapped to match the Russian PC keyboard layout:
     *
     *   PC /        → MS7004 .        (PERIOD, no shift)
     *   PC ? (S+/)  → MS7004 ,        (COMMA, no shift)
     *   PC \ *      → MS7004 \        (BACKSLASH, no shift — needs ЛАТ!)
     *   PC | (S+\)  → MS7004 /        (SLASH, no shift)
     *   PC `        → disabled (ёЁ has no MS7004 equivalent)
     *   PC ~ (S+`)  → disabled
     *   PC " (S+2)  → MS7004 Shift+2  (already correct, pass through)
     *   PC ; (S+4)  → MS7004 ;        (SEMI_PLUS, no shift)
     *   PC : (S+6)  → MS7004 :        (COLON_STAR, no shift)
     *   PC ? (S+7)  → MS7004 Shift+/  (SLASH, keep shift)
     *
     * Non-letter symbol keys (digits, -, =) use the same PC→MS7004
     * character remapping as ЛАТ mode, so *, (, ), _, =, + produce
     * the expected PC-label characters regardless of mode.
     *
     * (*) BACKSLASH scancode is a letter key in РУС mode (Э), so the
     *     caller must temporarily switch to ЛАТ for emission.  This is
     *     signalled by returning key=BACKSLASH with needsLatMode=true.
     */
    if (rusMode) {
        if (shifted) {
            switch (phys) {
            /* Russian PC layout overrides */
            case SDL_SCANCODE_SLASH:     return {ms0515::Key::Comma,      false}; /* , */
            case SDL_SCANCODE_BACKSLASH: return {ms0515::Key::Slash,      false}; /* / */
            case SDL_SCANCODE_GRAVE:     return {ms0515::Key::None,       false}; /* Ё disabled */
            case SDL_SCANCODE_4:         return {ms0515::Key::SemiPlus,  false}; /* ; */
            case SDL_SCANCODE_6:         return {ms0515::Key::ColonStar, false}; /* : */
            case SDL_SCANCODE_7:         return {ms0515::Key::Slash,      true};  /* ? */
            /* Same as ЛАТ: PC symbol → MS7004 character match */
            case SDL_SCANCODE_8:         return {ms0515::Key::ColonStar, true};  /* * */
            case SDL_SCANCODE_9:         return {ms0515::Key::Digit8,          true};  /* ( */
            case SDL_SCANCODE_0:         return {ms0515::Key::Digit9,          true};  /* ) */
            case SDL_SCANCODE_MINUS:     return {ms0515::Key::Underscore, false}; /* _ */
            case SDL_SCANCODE_EQUALS:    return {ms0515::Key::SemiPlus,  true};  /* + */
            default: break;
            }
        } else {
            switch (phys) {
            case SDL_SCANCODE_SLASH:     return {ms0515::Key::Period,     false}; /* . */
            case SDL_SCANCODE_GRAVE:     return {ms0515::Key::None,       false}; /* ё disabled */
            /* Same as ЛАТ */
            case SDL_SCANCODE_EQUALS:    return {ms0515::Key::MinusEq,   true};  /* = */
            default: break;
            }
        }
        return {base, shifted};
    }

    /* ── ЛАТ mode: character-based symbol remapping ────────────────────
     *
     * PC US keyboard label → MS7004 key + shift that produces the same
     * character.  Letters (A-Z) are already character-mapped by
     * sdlToMs7004 so they pass through unchanged.
     *
     * Shifted PC symbols (host Shift IS held):
     *   PC ~  (Shift+`) → MS7004 TILDE        (no shift)
     *   PC @  (Shift+2) → MS7004 AT            (no shift)
     *   PC ^  (Shift+6) → MS7004 CHE           (no shift)
     *   PC &  (Shift+7) → MS7004 Shift+6       (keep shift)
     *   PC *  (Shift+8) → MS7004 Shift+:       (keep shift)
     *   PC (  (Shift+9) → MS7004 Shift+8       (keep shift)
     *   PC )  (Shift+0) → MS7004 Shift+9       (keep shift)
     *   PC _  (Shift+-) → MS7004 UNDERSCORE    (no shift)
     *   PC +  (Shift+=) → MS7004 Shift+;       (keep shift)
     *   PC :  (Shift+;) → MS7004 COLON_STAR    (no shift)
     *   PC "  (Shift+') → MS7004 Shift+2       (keep shift)
     *
     * Unshifted PC symbols (host Shift NOT held):
     *   PC =  → MS7004 Shift+MINUS_EQ          (add shift)
     *   PC '  → MS7004 Shift+7                 (add shift)
     *
     * All other keys (letters, digits 1-9, [, ], \, ,, ., /, -, ;,
     * function keys, modifiers, numpad) keep the positional mapping
     * and the host Shift state as-is.
     */

    if (shifted) {
        switch (phys) {
        /* Shifted: target key changes, Shift released */
        case SDL_SCANCODE_GRAVE:        return {ms0515::Key::Tilde,        false}; /* ~ */
        case SDL_SCANCODE_2:            return {ms0515::Key::At,           false}; /* @ */
        case SDL_SCANCODE_6:            return {ms0515::Key::Che,          false}; /* ^ */
        case SDL_SCANCODE_MINUS:        return {ms0515::Key::Underscore,   false}; /* _ */
        case SDL_SCANCODE_SEMICOLON:    return {ms0515::Key::ColonStar,   false}; /* : */
        case SDL_SCANCODE_LEFTBRACKET:  return {ms0515::Key::LBracePipe,  false}; /* { */
        case SDL_SCANCODE_RIGHTBRACKET: return {ms0515::Key::RBraceLeftUp,false}; /* } */
        /* Shifted: target key changes, Shift kept */
        case SDL_SCANCODE_7:            return {ms0515::Key::Digit6,            true};  /* & */
        case SDL_SCANCODE_8:            return {ms0515::Key::ColonStar,   true};  /* * */
        case SDL_SCANCODE_9:            return {ms0515::Key::Digit8,            true};  /* ( */
        case SDL_SCANCODE_0:            return {ms0515::Key::Digit9,            true};  /* ) */
        case SDL_SCANCODE_EQUALS:       return {ms0515::Key::SemiPlus,    true};  /* + */
        case SDL_SCANCODE_APOSTROPHE:   return {ms0515::Key::Digit2,            true};  /* " */
        case SDL_SCANCODE_BACKSLASH:    return {ms0515::Key::LBracePipe,  true};  /* | */
        default: break;
        }
    } else {
        switch (phys) {
        /* Unshifted: target key changes, Shift added */
        case SDL_SCANCODE_GRAVE:      return {ms0515::Key::Digit7,           true};  /* ` → ' (no backtick on MS0515) */
        case SDL_SCANCODE_EQUALS:     return {ms0515::Key::MinusEq,    true};  /* = */
        case SDL_SCANCODE_APOSTROPHE: return {ms0515::Key::Digit7,           true};  /* ' */
        default: break;
        }
    }

    return {base, shifted};
}

} /* namespace ms0515_frontend */
