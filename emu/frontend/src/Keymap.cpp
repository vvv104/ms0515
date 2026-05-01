/*
 * Keymap.cpp — SDL physical scancode → MS7004 key mapping.
 *
 * Pure lookup, no state.  See Keymap.hpp for the design overview.
 */

#include "Keymap.hpp"

namespace ms0515_frontend {

ms7004_key_t sdlToMs7004(SDL_Scancode phys, bool rusMode)
{
    /* ── Mode-dependent letter and symbol mappings ──────────────────── */

    if (rusMode) {
        /* ЙЦУКЕН positional: host physical position → MS7004 key for the
         * Russian letter at that position on the real keyboard. */
        switch (phys) {
        /* Top letter row: Й Ц У К Е Н Г Ш Щ З Х Ъ */
        case SDL_SCANCODE_Q:            return MS7004_KEY_J;
        case SDL_SCANCODE_W:            return MS7004_KEY_C;
        case SDL_SCANCODE_E:            return MS7004_KEY_U;
        case SDL_SCANCODE_R:            return MS7004_KEY_K;
        case SDL_SCANCODE_T:            return MS7004_KEY_E;
        case SDL_SCANCODE_Y:            return MS7004_KEY_N;
        case SDL_SCANCODE_U:            return MS7004_KEY_G;
        case SDL_SCANCODE_I:            return MS7004_KEY_LBRACKET;
        case SDL_SCANCODE_O:            return MS7004_KEY_RBRACKET;
        case SDL_SCANCODE_P:            return MS7004_KEY_Z;
        case SDL_SCANCODE_LEFTBRACKET:  return MS7004_KEY_H;
        case SDL_SCANCODE_RIGHTBRACKET: return MS7004_KEY_HARDSIGN;

        /* Home row: Ф Ы В А П Р О Л Д Ж Э */
        case SDL_SCANCODE_A:            return MS7004_KEY_F;
        case SDL_SCANCODE_S:            return MS7004_KEY_Y;
        case SDL_SCANCODE_D:            return MS7004_KEY_W;
        case SDL_SCANCODE_F:            return MS7004_KEY_A;
        case SDL_SCANCODE_G:            return MS7004_KEY_P;
        case SDL_SCANCODE_H:            return MS7004_KEY_R;
        case SDL_SCANCODE_J:            return MS7004_KEY_O;
        case SDL_SCANCODE_K:            return MS7004_KEY_L;
        case SDL_SCANCODE_L:            return MS7004_KEY_D;
        case SDL_SCANCODE_SEMICOLON:    return MS7004_KEY_V;
        case SDL_SCANCODE_APOSTROPHE:   return MS7004_KEY_BACKSLASH;

        /* Bottom row: Я Ч С М И Т Ь Б Ю */
        case SDL_SCANCODE_Z:            return MS7004_KEY_Q;
        case SDL_SCANCODE_X:            return MS7004_KEY_CHE;
        case SDL_SCANCODE_C:            return MS7004_KEY_S;
        case SDL_SCANCODE_V:            return MS7004_KEY_M;
        case SDL_SCANCODE_B:            return MS7004_KEY_I;
        case SDL_SCANCODE_N:            return MS7004_KEY_T;
        case SDL_SCANCODE_M:            return MS7004_KEY_X;
        case SDL_SCANCODE_COMMA:        return MS7004_KEY_B;
        case SDL_SCANCODE_PERIOD:       return MS7004_KEY_AT;
        default: break; /* fall through to common */
        }
    } else {
        /* Latin character-based: host letter → MS7004 key with the
         * matching Latin character, regardless of physical position. */
        switch (phys) {
        case SDL_SCANCODE_A: return MS7004_KEY_A;
        case SDL_SCANCODE_B: return MS7004_KEY_B;
        case SDL_SCANCODE_C: return MS7004_KEY_C;
        case SDL_SCANCODE_D: return MS7004_KEY_D;
        case SDL_SCANCODE_E: return MS7004_KEY_E;
        case SDL_SCANCODE_F: return MS7004_KEY_F;
        case SDL_SCANCODE_G: return MS7004_KEY_G;
        case SDL_SCANCODE_H: return MS7004_KEY_H;
        case SDL_SCANCODE_I: return MS7004_KEY_I;
        case SDL_SCANCODE_J: return MS7004_KEY_J;
        case SDL_SCANCODE_K: return MS7004_KEY_K;
        case SDL_SCANCODE_L: return MS7004_KEY_L;
        case SDL_SCANCODE_M: return MS7004_KEY_M;
        case SDL_SCANCODE_N: return MS7004_KEY_N;
        case SDL_SCANCODE_O: return MS7004_KEY_O;
        case SDL_SCANCODE_P: return MS7004_KEY_P;
        case SDL_SCANCODE_Q: return MS7004_KEY_Q;
        case SDL_SCANCODE_R: return MS7004_KEY_R;
        case SDL_SCANCODE_S: return MS7004_KEY_S;
        case SDL_SCANCODE_T: return MS7004_KEY_T;
        case SDL_SCANCODE_U: return MS7004_KEY_U;
        case SDL_SCANCODE_V: return MS7004_KEY_V;
        case SDL_SCANCODE_W: return MS7004_KEY_W;
        case SDL_SCANCODE_X: return MS7004_KEY_X;
        case SDL_SCANCODE_Y: return MS7004_KEY_Y;
        case SDL_SCANCODE_Z: return MS7004_KEY_Z;

        /* Symbol keys in Latin mode */
        case SDL_SCANCODE_LEFTBRACKET:  return MS7004_KEY_LBRACKET;
        case SDL_SCANCODE_RIGHTBRACKET: return MS7004_KEY_RBRACKET;
        case SDL_SCANCODE_SEMICOLON:    return MS7004_KEY_SEMI_PLUS;
        case SDL_SCANCODE_APOSTROPHE:   return MS7004_KEY_TILDE;
        case SDL_SCANCODE_COMMA:        return MS7004_KEY_COMMA;
        case SDL_SCANCODE_PERIOD:       return MS7004_KEY_PERIOD;
        default: break; /* fall through to common */
        }
    }

    /* ── Mode-independent mappings ─────────────────────────────────── */
    switch (phys) {

    /* Modifiers */
    case SDL_SCANCODE_LSHIFT:   return MS7004_KEY_SHIFT_L;
    case SDL_SCANCODE_RSHIFT:   return MS7004_KEY_SHIFT_R;
    case SDL_SCANCODE_LCTRL:    return MS7004_KEY_CTRL;
    /* RCTRL is reserved as the host-mode toggle key (PhysicalKeyboard) */
    case SDL_SCANCODE_CAPSLOCK: return MS7004_KEY_CAPS;
    case SDL_SCANCODE_LALT:     return MS7004_KEY_COMPOSE;
    case SDL_SCANCODE_RALT:     return MS7004_KEY_RUSLAT;

    /* Toggle */
    /* (no host key for РУС/ЛАТ — only via OSK or a custom binding) */

    /* Digits */
    case SDL_SCANCODE_1: return MS7004_KEY_1;
    case SDL_SCANCODE_2: return MS7004_KEY_2;
    case SDL_SCANCODE_3: return MS7004_KEY_3;
    case SDL_SCANCODE_4: return MS7004_KEY_4;
    case SDL_SCANCODE_5: return MS7004_KEY_5;
    case SDL_SCANCODE_6: return MS7004_KEY_6;
    case SDL_SCANCODE_7: return MS7004_KEY_7;
    case SDL_SCANCODE_8: return MS7004_KEY_8;
    case SDL_SCANCODE_9: return MS7004_KEY_9;
    case SDL_SCANCODE_0: return MS7004_KEY_0;

    /* Digit-row symbols (same in both modes) */
    case SDL_SCANCODE_GRAVE:     return MS7004_KEY_LBRACE_PIPE;
    case SDL_SCANCODE_MINUS:     return MS7004_KEY_MINUS_EQ;
    case SDL_SCANCODE_EQUALS:    return MS7004_KEY_RBRACE_LEFTUP;
    case SDL_SCANCODE_BACKSLASH: return MS7004_KEY_BACKSLASH;
    case SDL_SCANCODE_SLASH:     return MS7004_KEY_SLASH;

    /* Whitespace / editing */
    case SDL_SCANCODE_SPACE:     return MS7004_KEY_SPACE;
    case SDL_SCANCODE_RETURN:    return MS7004_KEY_RETURN;
    case SDL_SCANCODE_TAB:       return MS7004_KEY_TAB;
    case SDL_SCANCODE_BACKSPACE: return MS7004_KEY_BS;

    /* Editing cluster */
    case SDL_SCANCODE_HOME:      return MS7004_KEY_FIND;
    case SDL_SCANCODE_INSERT:    return MS7004_KEY_INSERT;
    case SDL_SCANCODE_DELETE:    return MS7004_KEY_REMOVE;
    case SDL_SCANCODE_END:       return MS7004_KEY_SELECT;
    case SDL_SCANCODE_PAGEUP:    return MS7004_KEY_PREV;
    case SDL_SCANCODE_PAGEDOWN:  return MS7004_KEY_NEXT;

    /* Arrows */
    case SDL_SCANCODE_LEFT:  return MS7004_KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return MS7004_KEY_RIGHT;
    case SDL_SCANCODE_UP:    return MS7004_KEY_UP;
    case SDL_SCANCODE_DOWN:  return MS7004_KEY_DOWN;

    /* Function keys.
     * Note: SDL_SCANCODE_ESCAPE intentionally NOT mapped — host ESC is
     * reserved for the frontend to exit fullscreen (see main.cpp event
     * loop).  The guest's Ф11 is reachable via the host F11 key. */
    case SDL_SCANCODE_F1:    return MS7004_KEY_F1;
    case SDL_SCANCODE_F2:    return MS7004_KEY_F2;
    case SDL_SCANCODE_F3:    return MS7004_KEY_F3;
    case SDL_SCANCODE_F4:    return MS7004_KEY_F4;
    case SDL_SCANCODE_F5:    return MS7004_KEY_F5;
    case SDL_SCANCODE_F6:    return MS7004_KEY_F6;
    case SDL_SCANCODE_F7:    return MS7004_KEY_F7;
    case SDL_SCANCODE_F8:    return MS7004_KEY_F8;
    case SDL_SCANCODE_F9:    return MS7004_KEY_F9;
    case SDL_SCANCODE_F10:   return MS7004_KEY_F10;
    case SDL_SCANCODE_F11:   return MS7004_KEY_F11;
    case SDL_SCANCODE_F12:   return MS7004_KEY_F12;
    case SDL_SCANCODE_F13:   return MS7004_KEY_F13;

    /* PF keys (top of numpad on modern keyboards).
     * KP_DIVIDE, KP_MULTIPLY, KP_PLUS are handled in main.cpp as
     * symbol keys (/, *, +); PF2-4 are OSK-only. */
    case SDL_SCANCODE_NUMLOCKCLEAR: return MS7004_KEY_PF1;

    /* Numpad */
    case SDL_SCANCODE_KP_0:      return MS7004_KEY_KP0_WIDE;
    case SDL_SCANCODE_KP_1:      return MS7004_KEY_KP_1;
    case SDL_SCANCODE_KP_2:      return MS7004_KEY_KP_2;
    case SDL_SCANCODE_KP_3:      return MS7004_KEY_KP_3;
    case SDL_SCANCODE_KP_4:      return MS7004_KEY_KP_4;
    case SDL_SCANCODE_KP_5:      return MS7004_KEY_KP_5;
    case SDL_SCANCODE_KP_6:      return MS7004_KEY_KP_6;
    case SDL_SCANCODE_KP_7:      return MS7004_KEY_KP_7;
    case SDL_SCANCODE_KP_8:      return MS7004_KEY_KP_8;
    case SDL_SCANCODE_KP_9:      return MS7004_KEY_KP_9;
    case SDL_SCANCODE_KP_PERIOD: return MS7004_KEY_KP_DOT;
    case SDL_SCANCODE_KP_ENTER:  return MS7004_KEY_KP_ENTER;
    case SDL_SCANCODE_KP_MINUS:  return MS7004_KEY_KP_MINUS;
    case SDL_SCANCODE_KP_COMMA:  return MS7004_KEY_KP_COMMA;

    default: return MS7004_KEY_NONE;
    }
}

Ms7004Mapped sdlToMs7004Char(SDL_Scancode phys, bool shifted, bool rusMode)
{
    ms7004_key_t base = sdlToMs7004(phys, rusMode);

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
            case SDL_SCANCODE_SLASH:     return {MS7004_KEY_COMMA,      false}; /* , */
            case SDL_SCANCODE_BACKSLASH: return {MS7004_KEY_SLASH,      false}; /* / */
            case SDL_SCANCODE_GRAVE:     return {MS7004_KEY_NONE,       false}; /* Ё disabled */
            case SDL_SCANCODE_4:         return {MS7004_KEY_SEMI_PLUS,  false}; /* ; */
            case SDL_SCANCODE_6:         return {MS7004_KEY_COLON_STAR, false}; /* : */
            case SDL_SCANCODE_7:         return {MS7004_KEY_SLASH,      true};  /* ? */
            /* Same as ЛАТ: PC symbol → MS7004 character match */
            case SDL_SCANCODE_8:         return {MS7004_KEY_COLON_STAR, true};  /* * */
            case SDL_SCANCODE_9:         return {MS7004_KEY_8,          true};  /* ( */
            case SDL_SCANCODE_0:         return {MS7004_KEY_9,          true};  /* ) */
            case SDL_SCANCODE_MINUS:     return {MS7004_KEY_UNDERSCORE, false}; /* _ */
            case SDL_SCANCODE_EQUALS:    return {MS7004_KEY_SEMI_PLUS,  true};  /* + */
            default: break;
            }
        } else {
            switch (phys) {
            case SDL_SCANCODE_SLASH:     return {MS7004_KEY_PERIOD,     false}; /* . */
            case SDL_SCANCODE_GRAVE:     return {MS7004_KEY_NONE,       false}; /* ё disabled */
            /* Same as ЛАТ */
            case SDL_SCANCODE_EQUALS:    return {MS7004_KEY_MINUS_EQ,   true};  /* = */
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
        case SDL_SCANCODE_GRAVE:        return {MS7004_KEY_TILDE,        false}; /* ~ */
        case SDL_SCANCODE_2:            return {MS7004_KEY_AT,           false}; /* @ */
        case SDL_SCANCODE_6:            return {MS7004_KEY_CHE,          false}; /* ^ */
        case SDL_SCANCODE_MINUS:        return {MS7004_KEY_UNDERSCORE,   false}; /* _ */
        case SDL_SCANCODE_SEMICOLON:    return {MS7004_KEY_COLON_STAR,   false}; /* : */
        case SDL_SCANCODE_LEFTBRACKET:  return {MS7004_KEY_LBRACE_PIPE,  false}; /* { */
        case SDL_SCANCODE_RIGHTBRACKET: return {MS7004_KEY_RBRACE_LEFTUP,false}; /* } */
        /* Shifted: target key changes, Shift kept */
        case SDL_SCANCODE_7:            return {MS7004_KEY_6,            true};  /* & */
        case SDL_SCANCODE_8:            return {MS7004_KEY_COLON_STAR,   true};  /* * */
        case SDL_SCANCODE_9:            return {MS7004_KEY_8,            true};  /* ( */
        case SDL_SCANCODE_0:            return {MS7004_KEY_9,            true};  /* ) */
        case SDL_SCANCODE_EQUALS:       return {MS7004_KEY_SEMI_PLUS,    true};  /* + */
        case SDL_SCANCODE_APOSTROPHE:   return {MS7004_KEY_2,            true};  /* " */
        case SDL_SCANCODE_BACKSLASH:    return {MS7004_KEY_LBRACE_PIPE,  true};  /* | */
        default: break;
        }
    } else {
        switch (phys) {
        /* Unshifted: target key changes, Shift added */
        case SDL_SCANCODE_GRAVE:      return {MS7004_KEY_7,           true};  /* ` → ' (no backtick on MS0515) */
        case SDL_SCANCODE_EQUALS:     return {MS7004_KEY_MINUS_EQ,    true};  /* = */
        case SDL_SCANCODE_APOSTROPHE: return {MS7004_KEY_7,           true};  /* ' */
        default: break;
        }
    }

    return {base, shifted};
}

} /* namespace ms0515_frontend */
