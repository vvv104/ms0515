/*
 * ms7004.c — Elektronika MS 7004 keyboard microcontroller model.
 *
 * See ms7004.h for the architectural story.  This file implements the
 * LK201-derived state machine: press/release tracking, modifier
 * latching, ALL-UP on last-release, toggle keys, and auto-repeat.
 *
 * The public entry point is ms7004_key(key, down).  Everything the
 * real MS 7004 microcontroller does that matters to the guest lives
 * here; callers (OSK clicks, host-key events) stay dumb.
 */

#include <ms0515/ms7004.h>
#include <ms0515/keyboard.h>

#include <string.h>

/* ── Behaviour deviations (frontend only, this file is authentic) ─────
 *
 * The ms7004 model implements authentic MS7004 / LK201 behaviour.
 * The OSK click handler in OnScreenKeyboard.cpp applies four UX
 * overrides on top of this model.  Physical keyboard events go through
 * the model unmodified.
 *
 * [DEVIATE 1] ВР (Shift) + symbol-on-letter keys (Ш/[ Щ/] Э/\ Ч/¬)
 *             in ЛАТ mode:
 *     Real MS7004: Shift scancode is sent, ROM maps to { } | ~.
 *     OSK override: Shift is released before the key, so the ROM sees
 *     the unshifted symbol [ ] \ ^.
 *     In РУС mode these keys are treated as letters and respond to
 *     Shift normally (lowercase → uppercase Cyrillic).
 *
 * [DEVIATE 2] ФКС (CapsLock) + non-letter keys:
 *     Real MS7004: CAPS toggle affects the ROM's character mapping for
 *     all keys (the keyboard has no concept of "letter vs symbol").
 *     OSK override: for digits, symbols, function keys — the OSK
 *     temporarily toggles CAPS off before the keypress and back on
 *     after, so the ROM output is unchanged by CAPS.
 *     Which keys count as "letters" is mode-dependent: in РУС mode,
 *     Ш/Щ/Э/Ч/Ю/Ъ are letters; in ЛАТ mode they are symbols.
 *
 * [DEVIATE 3] ФКС + ВР + letter keys:
 *     Real MS7004: CAPS and Shift do not cancel — the result is
 *     uppercase (same as CAPS alone).
 *     OSK override: Shift inverts CAPS (modern CapsLock+Shift
 *     cancellation) — the result is lowercase.
 *
 * [DEVIATE 4] РУС/ЛАТ + non-letter keys:
 *     Real MS7004: the ROM's character table maps some symbol scancodes
 *     to Cyrillic letters in РУС mode (e.g. { → Ш, } → Щ, ~ → Ч).
 *     OSK override: for non-letter keys the OSK temporarily switches
 *     to ЛАТ before emitting the scancode and back to РУС after, so
 *     the symbol output is preserved regardless of mode.
 * ──────────────────────────────────────────────────────────────────────── */

/* ── Well-known scancodes ─────────────────────────────────────────────── */

#define SC_SHIFT    0256  /* ВР */
#define SC_CTRL     0257  /* СУ */
#define SC_CAPS     0260  /* ФКС (toggle) */
#define SC_COMPOSE  0261  /* КМП */
#define SC_RUSLAT   0262  /* РУС/ЛАТ (toggle) */
#define SC_ALLUP    0263  /* emitted when last held key is released */
#define SC_REPEAT   0254  /* auto-repeat code (sent instead of key's own scancode) */

/* ── Scancode table, indexed by ms7004_key_t ──────────────────────────── */

static const uint8_t kScancode[MS7004_KEY__COUNT] = {
    [MS7004_KEY_NONE]         = 0,

    /* Function strip.  F1-F13 taken from the working Keymap; F14-F20
     * and ПМ/ИСП use their canonical LK201 values.  The real MS 7004
     * ROM maps these to identical codes (LK201 compatibility). */
    [MS7004_KEY_F1]           = 0126,
    [MS7004_KEY_F2]           = 0127,
    [MS7004_KEY_F3]           = 0130,
    [MS7004_KEY_F4]           = 0131,
    [MS7004_KEY_F5]           = 0132,
    [MS7004_KEY_F6]           = 0144,
    [MS7004_KEY_F7]           = 0145,
    [MS7004_KEY_F8]           = 0146,
    [MS7004_KEY_F9]           = 0147,
    [MS7004_KEY_F10]          = 0150,
    [MS7004_KEY_F11]          = 0161,
    [MS7004_KEY_F12]          = 0162,
    [MS7004_KEY_F13]          = 0163,
    [MS7004_KEY_F14]          = 0164,
    [MS7004_KEY_HELP]         = 0174,  /* ПМ (Помощь) — LK201 HELP slot */
    [MS7004_KEY_PERFORM]      = 0175,  /* ИСП — LK201 DO slot */
    [MS7004_KEY_F17]          = 0200,
    [MS7004_KEY_F18]          = 0201,
    [MS7004_KEY_F19]          = 0202,
    [MS7004_KEY_F20]          = 0203,

    /* Row 1 */
    [MS7004_KEY_LBRACE_PIPE]  = 0374,
    [MS7004_KEY_SEMI_PLUS]    = 0277,
    [MS7004_KEY_1]            = 0300,
    [MS7004_KEY_2]            = 0305,
    [MS7004_KEY_3]            = 0313,
    [MS7004_KEY_4]            = 0320,
    [MS7004_KEY_5]            = 0325,
    [MS7004_KEY_6]            = 0333,
    [MS7004_KEY_7]            = 0340,
    [MS7004_KEY_8]            = 0345,
    [MS7004_KEY_9]            = 0352,
    [MS7004_KEY_0]            = 0357,
    [MS7004_KEY_MINUS_EQ]     = 0371,
    [MS7004_KEY_RBRACE_LEFTUP] = 0365,
    [MS7004_KEY_BS]           = 0274,

    /* Row 2 */
    [MS7004_KEY_TAB]          = 0276,
    [MS7004_KEY_J]            = 0301,
    [MS7004_KEY_C]            = 0306,
    [MS7004_KEY_U]            = 0314,
    [MS7004_KEY_K]            = 0321,
    [MS7004_KEY_E]            = 0327,
    [MS7004_KEY_N]            = 0334,
    [MS7004_KEY_G]            = 0341,
    [MS7004_KEY_LBRACKET]     = 0346,
    [MS7004_KEY_RBRACKET]     = 0353,
    [MS7004_KEY_Z]            = 0360,
    [MS7004_KEY_H]            = 0366,
    [MS7004_KEY_COLON_STAR]   = 0372,
    [MS7004_KEY_TILDE]        = 0304,
    [MS7004_KEY_RETURN]       = 0275,

    /* Row 3 */
    [MS7004_KEY_CTRL]         = SC_CTRL,
    [MS7004_KEY_CAPS]         = SC_CAPS,
    [MS7004_KEY_F]            = 0302,
    [MS7004_KEY_Y]            = 0307,
    [MS7004_KEY_W]            = 0315,
    [MS7004_KEY_A]            = 0322,
    [MS7004_KEY_P]            = 0330,
    [MS7004_KEY_R]            = 0335,
    [MS7004_KEY_O]            = 0342,
    [MS7004_KEY_L]            = 0347,
    [MS7004_KEY_D]            = 0354,
    [MS7004_KEY_V]            = 0362,
    [MS7004_KEY_BACKSLASH]    = 0373,
    [MS7004_KEY_PERIOD]       = 0367,
    [MS7004_KEY_HARDSIGN]     = 0361,

    /* Row 4 */
    [MS7004_KEY_SHIFT_L]      = SC_SHIFT,
    [MS7004_KEY_RUSLAT]       = SC_RUSLAT,
    [MS7004_KEY_Q]            = 0303,
    [MS7004_KEY_CHE]          = 0310,
    [MS7004_KEY_S]            = 0316,
    [MS7004_KEY_M]            = 0323,
    [MS7004_KEY_I]            = 0331,
    [MS7004_KEY_T]            = 0336,
    [MS7004_KEY_X]            = 0343,
    [MS7004_KEY_B]            = 0350,
    [MS7004_KEY_AT]           = 0355,
    [MS7004_KEY_COMMA]        = 0363,
    [MS7004_KEY_SLASH]        = 0312,
    [MS7004_KEY_UNDERSCORE]   = 0361,
    [MS7004_KEY_SHIFT_R]      = SC_SHIFT,

    /* Row 5 */
    [MS7004_KEY_COMPOSE]      = SC_COMPOSE,
    [MS7004_KEY_SPACE]        = 0324,
    [MS7004_KEY_KP0_WIDE]     = 0222,
    [MS7004_KEY_KP_ENTER]     = 0225,

    /* Editing cluster */
    [MS7004_KEY_FIND]         = 0212,
    [MS7004_KEY_INSERT]       = 0213,
    [MS7004_KEY_REMOVE]       = 0214,
    [MS7004_KEY_SELECT]       = 0215,
    [MS7004_KEY_PREV]         = 0216,
    [MS7004_KEY_NEXT]         = 0217,

    /* Arrows */
    [MS7004_KEY_UP]           = 0252,
    [MS7004_KEY_DOWN]         = 0251,
    [MS7004_KEY_LEFT]         = 0247,
    [MS7004_KEY_RIGHT]        = 0250,

    /* PF */
    [MS7004_KEY_PF1]          = 0241,
    [MS7004_KEY_PF2]          = 0242,
    [MS7004_KEY_PF3]          = 0243,
    [MS7004_KEY_PF4]          = 0244,

    /* Numpad */
    [MS7004_KEY_KP_1]         = 0226,
    [MS7004_KEY_KP_2]         = 0227,
    [MS7004_KEY_KP_3]         = 0230,
    [MS7004_KEY_KP_4]         = 0231,
    [MS7004_KEY_KP_5]         = 0232,
    [MS7004_KEY_KP_6]         = 0233,
    [MS7004_KEY_KP_7]         = 0235,
    [MS7004_KEY_KP_8]         = 0236,
    [MS7004_KEY_KP_9]         = 0237,
    [MS7004_KEY_KP_DOT]       = 0224,
    [MS7004_KEY_KP_COMMA]     = 0234,
    [MS7004_KEY_KP_MINUS]     = 0240,
};

/* ── Key kind classification ──────────────────────────────────────────── */

static bool is_toggle(ms7004_key_t k)
{
    return k == MS7004_KEY_CAPS || k == MS7004_KEY_RUSLAT;
}

static bool is_modifier(ms7004_key_t k)
{
    return k == MS7004_KEY_CTRL
        || k == MS7004_KEY_SHIFT_L
        || k == MS7004_KEY_SHIFT_R
        || k == MS7004_KEY_COMPOSE;
}

static bool no_repeat(ms7004_key_t k)
{
    return (k >= MS7004_KEY_F1 && k <= MS7004_KEY_F20)
        || (k >= MS7004_KEY_PF1 && k <= MS7004_KEY_PF4)
        || k == MS7004_KEY_FIND
        || k == MS7004_KEY_SELECT
        || k == MS7004_KEY_INSERT;
}

static bool key_valid(ms7004_key_t k)
{
    return k > MS7004_KEY_NONE && k < MS7004_KEY__COUNT;
}

/* ── Downstream emission ──────────────────────────────────────────────── */

static void emit(ms7004_t *kbd, uint8_t sc)
{
    if (sc != 0 && kbd->uart)
        kbd_push_scancode(kbd->uart, sc);
}

/* ── Lifecycle ────────────────────────────────────────────────────────── */

void ms7004_init(ms7004_t *kbd, struct ms0515_keyboard *uart)
{
    memset(kbd, 0, sizeof(*kbd));
    kbd->uart             = uart;
    kbd->repeat_delay_ms  = 500;
    kbd->repeat_period_ms = 33;
    kbd->repeat_enabled   = false;
    kbd->data_enabled     = true;
    kbd->sound_enabled    = true;
    kbd->click_enabled    = true;
}

void ms7004_reset(ms7004_t *kbd)
{
    struct ms0515_keyboard *uart = kbd->uart;
    ms7004_init(kbd, uart);
}

/* ── Main entry point ─────────────────────────────────────────────────── */

void ms7004_key(ms7004_t *kbd, ms7004_key_t key, bool down)
{
    if (!key_valid(key)) return;

    if (down) {
        if (kbd->held[key]) return;   /* already pressed, ignore */

        if (is_toggle(key)) {
            /* Toggle keys: emit once, flip internal flag.  They do not
             * participate in held_count — the real firmware latches
             * the state and does not emit a release code. */
            emit(kbd, kScancode[key]);
            if (key == MS7004_KEY_CAPS)   kbd->caps_on   = !kbd->caps_on;
            if (key == MS7004_KEY_RUSLAT) kbd->ruslat_on = !kbd->ruslat_on;
            return;
        }

        /* Both modifiers and regular keys: mark held, emit make code. */
        kbd->held[key] = 1;
        kbd->held_count++;
        emit(kbd, kScancode[key]);

        /* Auto-repeat tracks the most recent regular keypress.  A
         * modifier going down cancels the pending repeat (matches
         * LK201: repeat only while the last non-modifier is held
         * alone). */
        if (is_modifier(key) || no_repeat(key)) {
            kbd->repeat_key = MS7004_KEY_NONE;
        } else {
            /* Push onto key stack (bounded, oldest entries fall off). */
            if (kbd->key_stack_top < 8)
                kbd->key_stack[kbd->key_stack_top++] = key;

            kbd->repeat_key     = key;
            kbd->repeat_next_ms = kbd->now_ms + kbd->repeat_delay_ms;
        }
        return;
    }

    /* Release path. */
    if (is_toggle(key)) return;         /* no-op */
    if (!kbd->held[key]) return;        /* wasn't down */

    kbd->held[key] = 0;
    if (kbd->held_count > 0) kbd->held_count--;

    /* Remove released key from the stack. */
    for (int i = 0; i < kbd->key_stack_top; ++i) {
        if (kbd->key_stack[i] == key) {
            for (int j = i; j < kbd->key_stack_top - 1; ++j)
                kbd->key_stack[j] = kbd->key_stack[j + 1];
            kbd->key_stack_top--;
            break;
        }
    }

    if (kbd->repeat_key == key) {
        /* Fall back to the previous still-held regular key. */
        if (kbd->key_stack_top > 0) {
            kbd->repeat_key     = kbd->key_stack[kbd->key_stack_top - 1];
            kbd->repeat_next_ms = kbd->now_ms + kbd->repeat_delay_ms;
        } else {
            kbd->repeat_key = MS7004_KEY_NONE;
        }
    }

    /* ALL-UP is emitted only when the LAST held key (modifier OR
     * regular) is released — matches LK201 behaviour the boot ROM
     * depends on. */
    if (kbd->held_count == 0)
        emit(kbd, SC_ALLUP);
}

void ms7004_release_all(ms7004_t *kbd)
{
    bool any = false;
    for (int i = 0; i < MS7004_KEY__COUNT; ++i) {
        if (kbd->held[i]) { kbd->held[i] = 0; any = true; }
    }
    kbd->held_count    = 0;
    kbd->repeat_key    = MS7004_KEY_NONE;
    kbd->key_stack_top = 0;
    if (any) emit(kbd, SC_ALLUP);
}

/* ── Time tick / auto-repeat ──────────────────────────────────────────── */

void ms7004_tick(ms7004_t *kbd, uint32_t now_ms)
{
    kbd->now_ms = now_ms;

    if (!kbd->repeat_enabled) return;
    if (!key_valid(kbd->repeat_key)) return;
    if (!kbd->held[kbd->repeat_key]) { kbd->repeat_key = MS7004_KEY_NONE; return; }

    while ((int32_t)(now_ms - kbd->repeat_next_ms) >= 0) {
        emit(kbd, kScancode[kbd->repeat_key]);
        kbd->repeat_next_ms += kbd->repeat_period_ms;
        if (kbd->repeat_period_ms == 0) break;  /* guard */
    }
}

/* ── Host → keyboard command channel ──────────────────────────────────── */

/*
 * MS7004 host→keyboard command protocol (from ТО, Table 3).
 *
 * Single-byte commands:
 *   0o021 (0x11)  Latin indicator ON
 *   0o210 (0x88)  Data output disabled
 *   0o213 (0x8B)  Data output enabled
 *   0o231 (0x99)  Keyclick disabled
 *   0o237 (0x9F)  Produce click sound
 *   0o241 (0xA1)  Sound disabled
 *   0o247 (0xA7)  Produce bell sound
 *   0o220 (0x90)  Auto-repeat enabled
 *   0o343 (0xE3)  Auto-repeat enabled (LK201 alternate)
 *   0o253 (0xAB)  ID probe → response 0o001, 0o000
 *   0o341 (0xE1)  Auto-repeat disabled
 *   0o331 (0xD9)  Auto-repeat disabled (alternate)
 *   0o375 (0xFD)  Power-up reset → response 0o001, 0o000, 0o000, 0o000
 *
 * Two-byte commands (first byte is a prefix):
 *   0o043 (0x23) + 0o2XX         Sound enabled (second byte = volume)
 *   0o033 (0x1B) + 0o2XX         Keyclick enabled (second byte = volume)
 *
 * LED control (2-byte, mode + mask):
 *   mode byte: 0o023 (0x13) = ON, 0o021 (0x11) = OFF
 *   mask byte 0o200–0o217: bits 0–3 select LEDs
 *     bit 0 = Wait, bit 1 = Compose, bit 2 = CapsLock, bit 3 = Hold
 *   The ROM sends mode first, then mask (CALL 177042 sends R3 lo byte
 *   first).  When no valid mask follows, 0x11/0x13 act as standalone
 *   Latin indicator ON/OFF commands.
 */

/*
 * Try to complete a 2-byte command.  Returns true if the second byte
 * was accepted (valid combination), false if it was not a valid second
 * byte for this prefix — caller should re-process it as a new command.
 *
 * On real hardware, an invalid/missing second byte triggers a 100ms
 * timeout and error code 0o266.  We approximate this by immediately
 * cancelling the pending prefix when the second byte doesn't match.
 */
static bool cmd_second_byte(ms7004_t *kbd, uint8_t first, uint8_t second)
{
    switch (first) {
    case 0x23:  /* 0o043 — sound enabled (second byte = volume) */
        kbd->sound_enabled = true;
        return true;

    case 0x1B:  /* 0o033 — keyclick enabled (second byte = volume) */
        kbd->click_enabled = true;
        return true;

    /* LED control: mode byte first (0x11=OFF, 0x13=ON), then mask byte.
     * The ROM's CALL 177042 sends mode first, mask second (e.g. R3=0x8413
     * → sends 0x13 then 0x84 for CapsLock LED ON).
     * If the second byte is not a valid LED mask, the mode byte acts as
     * a standalone Latin indicator command (0x11=ON, 0x13=OFF), and the
     * second byte is re-processed as a new command (return false). */
    case 0x11:  /* LED OFF mode / Latin indicator ON */
        if (second >= 0x80 && second <= 0x8F) {
            /* Valid LED mask — turn off selected LEDs (bits 0-3).
             * We don't track individual LED state, just accept. */
            return true;
        }
        /* No valid mask — apply standalone Latin indicator ON. */
        kbd->latin_indicator = true;
        return false;

    case 0x13:  /* LED ON mode / Latin indicator OFF */
        if (second >= 0x80 && second <= 0x8F) {
            /* Valid LED mask — turn on selected LEDs. */
            return true;
        }
        /* No valid mask — apply standalone Latin indicator OFF. */
        kbd->latin_indicator = false;
        return false;

    default:
        return false;
    }
}

void ms7004_host_byte(ms7004_t *kbd, uint8_t byte)
{
    /* If we're waiting for the second byte of a 2-byte command: */
    if (kbd->cmd_pending) {
        uint8_t first = kbd->cmd_pending;
        kbd->cmd_pending = 0;
        if (cmd_second_byte(kbd, first, byte))
            return;
        /* Second byte not valid for this prefix — cancel pending,
         * fall through to process 'byte' as a new command. */
    }

    /* Single-byte commands and 2-byte prefixes. */
    switch (byte) {
    /* ── ID probe ────────────────────────────────────────────────── */
    case 0xAB:  /* 0o253 — respond with 0o001, 0o000 */
        if (kbd->uart) {
            kbd_flush_fifo(kbd->uart);
            kbd_push_scancode(kbd->uart, 0x01);
            kbd_push_scancode(kbd->uart, 0x00);
        }
        return;

    /* ── Power-up reset ──────────────────────────────────────────── */
    case 0xFD:  /* 0o375 — respond with 0o001, 0o000, 0o000, 0o000 */
        kbd->repeat_enabled  = false;
        kbd->data_enabled    = true;
        kbd->sound_enabled   = true;
        kbd->click_enabled   = true;
        kbd->latin_indicator = false;
        kbd->repeat_delay_ms  = 500;
        kbd->repeat_period_ms = 33;
        if (kbd->uart) {
            kbd_flush_fifo(kbd->uart);
            kbd_push_scancode(kbd->uart, 0x01);
            kbd_push_scancode(kbd->uart, 0x00);
            kbd_push_scancode(kbd->uart, 0x00);
            kbd_push_scancode(kbd->uart, 0x00);
        }
        return;

    /* ── Auto-repeat enable ──────────────────────────────────────── */
    case 0x90:  /* 0o220 — MS7004 auto-repeat enable */
    case 0xE3:  /* 0o343 — LK201 auto-repeat enable (alternate) */
        kbd->repeat_enabled   = true;
        kbd->repeat_delay_ms  = 500;  /* restore normal OS typing rate */
        kbd->repeat_period_ms = 33;
        /* fprintf(stderr, "[KBD] auto-repeat ENABLED\n"); */
        return;

    /* ── Auto-repeat disable ─────────────────────────────────────── */
    case 0xE1:  /* 0o341 */
    case 0xD9:  /* 0o331 */
        kbd->repeat_enabled = false;
        /* fprintf(stderr, "[KBD] auto-repeat DISABLED\n"); */
        return;

    /* ── LED mode / Latin indicator (2-byte prefix) ────────────────
     * 0x11 = LED OFF mode (+ mask) or Latin indicator ON (standalone)
     * 0x13 = LED ON mode (+ mask) or Latin indicator OFF (standalone)
     * The ROM sends mode first, then mask (e.g. CapsLock ON = 0x13, 0x84).
     * If no valid mask follows, the command acts as Latin indicator. */
    case 0x11:  /* 0o021 */
    case 0x13:  /* 0o023 */
        kbd->cmd_pending = byte;
        return;

    /* ── Data output control ─────────────────────────────────────── */
    case 0x88:  /* 0o210 — data output disabled */
        kbd->data_enabled = false;
        return;
    case 0x8B:  /* 0o213 — data output enabled */
        kbd->data_enabled = true;
        return;

    /* ── Sound ───────────────────────────────────────────────────── */
    case 0xA7:  /* 0o247 — produce bell */
        /* TODO: could trigger a host beep if wired up */
        return;
    case 0x9F:  /* 0o237 — produce click */
        return;
    case 0xA1:  /* 0o241 — sound disabled */
        kbd->sound_enabled = false;
        return;
    case 0x99:  /* 0o231 — keyclick disabled */
        kbd->click_enabled = false;
        /* Games send this command; disable auto-repeat so held keys
         * produce a single make code + ALL-UP on release. */
        kbd->repeat_enabled = false;
        return;

    /* ── 2-byte command prefixes ─────────────────────────────────── */
    case 0x23:  /* 0o043 — sound enable + volume */
    case 0x1B:  /* 0o033 — keyclick enable + volume */
        kbd->cmd_pending = byte;
        return;

    default:
        /* fprintf(stderr, "[KBD] unknown host command: 0x%02X\n", byte); */
        return;
    }
}

/* ── Queries ──────────────────────────────────────────────────────────── */

bool ms7004_caps_on  (const ms7004_t *kbd) { return kbd->caps_on; }
bool ms7004_ruslat_on(const ms7004_t *kbd) { return kbd->ruslat_on; }

bool ms7004_is_held(const ms7004_t *kbd, ms7004_key_t key)
{
    if (!key_valid(key)) return false;
    return kbd->held[key] != 0;
}

uint8_t ms7004_scancode(ms7004_key_t key)
{
    if (!key_valid(key)) return 0;
    return kScancode[key];
}
