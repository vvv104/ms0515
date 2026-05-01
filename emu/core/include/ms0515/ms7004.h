/*
 * ms7004.h — Elektronika MS 7004 keyboard microcontroller model.
 *
 * The MS 7004 is a serial keyboard derived from the DEC LK201.  A small
 * microcontroller inside the keyboard scans the key matrix, tracks modifier
 * state, handles auto-repeat, and emits 8-bit scancodes over a 4800-baud
 * serial line.  Its output is consumed by the host's i8251 USART (modelled
 * separately in keyboard.c); this module models the keyboard itself.
 *
 * Design goals
 * ------------
 * `core/keyboard.c` is a pure i8251 USART — it does not know what bytes
 * mean.  This module sits logically on the serial line's *other* end: the
 * frontend tells it "physical key at matrix position X went down", and it
 * produces the scancode sequence the real MS 7004 would have emitted,
 * pushing bytes into the USART's RX FIFO.
 *
 * That way, OSK clicks and physical host-key events funnel through the
 * same single point (`ms7004_key`), and all MS 7004 behaviour (modifier
 * latching, ALL-UP on release, toggle keys, auto-repeat) lives here — the
 * one authoritative place, matching the real hardware.
 *
 * Scancode semantics (LK201-derived)
 * ----------------------------------
 *   - Regular key pressed : emit its scancode once.
 *   - Held modifier (ВР/СУ/КМП) pressed : emit its scancode, set internal
 *     "held" flag.
 *   - Held modifier released : emit nothing immediately; emit ALL-UP
 *     (0o263) only when the LAST held key (regular OR modifier) is
 *     released.  This matches LK201 behaviour and is what the MS0515
 *     boot ROM expects.
 *   - Toggle key (ФКС, РУС/ЛАТ) pressed : emit its scancode once, flip
 *     an internal toggle flag.  Release is a no-op.  The keyboard
 *     firmware tracks the toggle state; the host ROM reads it back by
 *     inference from subsequent keystrokes, not by polling.
 *   - Auto-repeat : while a regular key is held alone (as the most
 *     recent non-modifier press), its scancode may be re-emitted at
 *     a fixed rate after an initial delay.  Disabled by default in
 *     this model until a host command enables it.
 *
 * Sources
 * -------
 *   - MS0515BTL emulator (nzeemin/ms0515btl) — full scancode table in
 *     KeyboardView.cpp; treated as the authoritative make-code set.
 *   - DEC LK201 Technical Manual (EK-104AA-TM) — modifier & ALL-UP
 *     semantics, auto-repeat divisions.
 *   - Habr article about MS 7004 (timeweb/706422) — protocol overview.
 *   - NS4 technical description §4.10 — USART interface only; the doc
 *     does not describe keyboard-side behaviour.
 */

#ifndef MS0515_MS7004_H
#define MS0515_MS7004_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ms0515_keyboard;      /* forward decl — see keyboard.h */

/*
 * Physical key identifiers.  Each enum value corresponds to ONE cap on
 * the real MS 7004.  Values are opaque indices; the matrix-position →
 * scancode mapping is internal (see ms7004.c::kScancode).
 *
 * Ordering below follows the physical layout (top strip, then main
 * block row-by-row, then editing cluster / arrows / PF / numpad).  Do
 * not reorder arbitrarily — the scancode table in ms7004.c is index-
 * aligned with this enum.
 */
typedef enum ms7004_key {
    MS7004_KEY_NONE = 0,

    /* ── Function strip (top row) ────────────────────────────────── */
    MS7004_KEY_F1,  MS7004_KEY_F2,  MS7004_KEY_F3,  MS7004_KEY_F4,
    MS7004_KEY_F5,
    MS7004_KEY_F6,  MS7004_KEY_F7,  MS7004_KEY_F8,  MS7004_KEY_F9,
    MS7004_KEY_F10,
    MS7004_KEY_F11, MS7004_KEY_F12, MS7004_KEY_F13, MS7004_KEY_F14,
    MS7004_KEY_HELP,        /* ПМ (Помощь) */
    MS7004_KEY_PERFORM,     /* ИСП */
    MS7004_KEY_F17, MS7004_KEY_F18, MS7004_KEY_F19, MS7004_KEY_F20,

    /* ── Row 1: digit row ────────────────────────────────────────── */
    MS7004_KEY_LBRACE_PIPE,     /* "{|"  sc 0o374 */
    MS7004_KEY_SEMI_PLUS,       /* ";+"  sc 0o277 */
    MS7004_KEY_1, MS7004_KEY_2, MS7004_KEY_3, MS7004_KEY_4,
    MS7004_KEY_5, MS7004_KEY_6, MS7004_KEY_7, MS7004_KEY_8,
    MS7004_KEY_9, MS7004_KEY_0,
    MS7004_KEY_MINUS_EQ,        /* "-="  sc 0o371 */
    MS7004_KEY_RBRACE_LEFTUP,   /* "}↖"  sc 0o365 */
    MS7004_KEY_BS,              /* ЗБ    sc 0o274 */

    /* ── Row 2: top letter row ───────────────────────────────────── */
    MS7004_KEY_TAB,             /* ТАБ   sc 0o276 */
    MS7004_KEY_J, MS7004_KEY_C, MS7004_KEY_U, MS7004_KEY_K,
    MS7004_KEY_E, MS7004_KEY_N, MS7004_KEY_G,
    MS7004_KEY_LBRACKET,        /* "Ш/[" sc 0o346 */
    MS7004_KEY_RBRACKET,        /* "Щ/]" sc 0o353 */
    MS7004_KEY_Z,               /* "З/Z" sc 0o360 */
    MS7004_KEY_H,               /* "Х/H" sc 0o366 */
    MS7004_KEY_COLON_STAR,      /* ":*"  sc 0o372 */
    MS7004_KEY_TILDE,           /* "~"   sc 0o304 */
    MS7004_KEY_RETURN,          /* ВК    sc 0o275 */

    /* ── Row 3: home row ─────────────────────────────────────────── */
    MS7004_KEY_CTRL,            /* СУ    sc 0o257 (held) */
    MS7004_KEY_CAPS,            /* ФКС   sc 0o260 (toggle) */
    MS7004_KEY_F, MS7004_KEY_Y, MS7004_KEY_W, MS7004_KEY_A,
    MS7004_KEY_P, MS7004_KEY_R, MS7004_KEY_O, MS7004_KEY_L,
    MS7004_KEY_D,
    MS7004_KEY_V,               /* "Ж/V"  sc 0o362 */
    MS7004_KEY_BACKSLASH,       /* "Э\"   sc 0o373 */
    MS7004_KEY_PERIOD,          /* ".>"   sc 0o367 */
    MS7004_KEY_HARDSIGN,        /* Ъ      sc 0o361  (same sc as `_` —
                                   see note in ms7004.c) */

    /* ── Row 4: bottom letter row ────────────────────────────────── */
    MS7004_KEY_SHIFT_L,         /* ВР left  sc 0o256 (held) */
    MS7004_KEY_RUSLAT,          /* РУС/ЛАТ  sc 0o262 (toggle) */
    MS7004_KEY_Q,               /* "Я/Q"  sc 0o303 */
    MS7004_KEY_CHE,             /* "Ч/¬"  sc 0o310 — no SDL keycode */
    MS7004_KEY_S, MS7004_KEY_M, MS7004_KEY_I, MS7004_KEY_T,
    MS7004_KEY_X,               /* "Ь/X"  sc 0o343 */
    MS7004_KEY_B,
    MS7004_KEY_AT,              /* "Ю/@"  sc 0o355 */
    MS7004_KEY_COMMA,           /* ",<"   sc 0o363 */
    MS7004_KEY_SLASH,           /* "/?"   sc 0o312 */
    MS7004_KEY_UNDERSCORE,      /* "_"    sc 0o361 */
    MS7004_KEY_SHIFT_R,         /* ВР right — shares scancode 0o256 */

    /* ── Row 5: bottom row (space etc.) ──────────────────────────── */
    MS7004_KEY_COMPOSE,         /* КМП    sc 0o261 (held) */
    MS7004_KEY_SPACE,           /*        sc 0o324 */
    MS7004_KEY_KP0_WIDE,        /* wide 0 sc 0o222 */
    MS7004_KEY_KP_ENTER,        /* ВВОД   sc 0o225 */

    /* ── Editing cluster ─────────────────────────────────────────── */
    MS7004_KEY_FIND,            /* НТ         sc 0o212 */
    MS7004_KEY_INSERT,          /* ВСТ        sc 0o213 */
    MS7004_KEY_REMOVE,          /* УДАЛ       sc 0o214 */
    MS7004_KEY_SELECT,          /* ВЫБР       sc 0o215 */
    MS7004_KEY_PREV,            /* ПРЕД.КАДР  sc 0o216 */
    MS7004_KEY_NEXT,            /* СЛЕД.КАДР  sc 0o217 */

    /* ── Arrows ──────────────────────────────────────────────────── */
    MS7004_KEY_UP,              /* sc 0o252 */
    MS7004_KEY_DOWN,            /* sc 0o251 */
    MS7004_KEY_LEFT,            /* sc 0o247 */
    MS7004_KEY_RIGHT,           /* sc 0o250 */

    /* ── PF keys (top of numpad) ─────────────────────────────────── */
    MS7004_KEY_PF1, MS7004_KEY_PF2, MS7004_KEY_PF3, MS7004_KEY_PF4,

    /* ── Numpad ─────────────────────────────────────────────────── */
    MS7004_KEY_KP_1, MS7004_KEY_KP_2, MS7004_KEY_KP_3,
    MS7004_KEY_KP_4, MS7004_KEY_KP_5, MS7004_KEY_KP_6,
    MS7004_KEY_KP_7, MS7004_KEY_KP_8, MS7004_KEY_KP_9,
    MS7004_KEY_KP_DOT,          /* sc 0o224 */
    MS7004_KEY_KP_COMMA,        /* sc 0o234 */
    MS7004_KEY_KP_MINUS,        /* sc 0o240 */

    MS7004_KEY__COUNT
} ms7004_key_t;

/* ── Public state ─────────────────────────────────────────────────────── */

typedef struct ms7004 {
    /* Downstream USART the keyboard is wired to. */
    struct ms0515_keyboard *uart;

    /* Per-key held state (1 = pressed on matrix). Indexed by ms7004_key_t. */
    uint8_t  held[MS7004_KEY__COUNT];

    /* Total number of held keys — both modifiers and regular.  ALL-UP
     * (0o263) is emitted when this transitions from non-zero back to
     * zero, *but only if a modifier was held during the session*.
     * Modifier-free key presses do not produce ALL-UP — emitting it
     * unconditionally exposes a host-side bug where ROM kbd routines
     * leave R0 untouched on byte 0o263 and the OS's interrupt handler
     * then leaks R0 from interrupted code (hits e.g. random reboots
     * in SABOT2, vec-130 halt in Mihin manual-D). */
    int      held_count;
    bool     modifier_in_session;    /* set on modifier press, cleared on ALL-UP */

    /* Toggle state, latched inside the keyboard firmware. */
    bool     caps_on;
    bool     ruslat_on;         /* false = ЛАТ, true = РУС */

    /* Auto-repeat: the most recently pressed regular key and the
     * time its next repeat is due.  The key_stack tracks the order
     * of regular (non-modifier, non-toggle) key presses so that when
     * the topmost key is released, repeat falls back to the previous
     * still-held key. */
    ms7004_key_t repeat_key;
    uint32_t     repeat_next_ms;
    uint32_t     repeat_delay_ms;    /* initial delay before first repeat */
    uint32_t     repeat_period_ms;   /* interval between repeats */
    bool         repeat_enabled;

    /* Auto game-mode (non-authentic convenience feature):
     *
     * The MS-7004 firmware uses a single typematic delay/period
     * (500 ms / 33 ms) tuned for typing.  Games inherit this and
     * feel sluggish — character takes one step, pauses 500 ms, then
     * runs.  Games typically signal "this is gameplay, not typing"
     * by sending 0x99 (click off) on entry; restore happens on click
     * re-enable, full reset, or BIOS POST after Ctrl-C reboot.
     *
     * When `auto_game_mode` is enabled, we observe these commands and
     * swap delay/period between two presets; the typing preset is
     * authoritative — game mode borrows from it on the way in.  When
     * disabled, only the typing preset is used. */
    bool         auto_game_mode;     /* heuristic enabled (default true) */
    bool         in_game_mode;       /* current state (true = game preset active) */
    uint32_t     repeat_typing_delay_ms;   /* preset for typing (default 500) */
    uint32_t     repeat_typing_period_ms;  /* preset for typing (default 33) */
    uint32_t     repeat_game_delay_ms;     /* preset for game   (default 50) */
    uint32_t     repeat_game_period_ms;    /* preset for game   (default 50) */

    ms7004_key_t key_stack[8];       /* recent regular keys, [top-1] is newest */
    int          key_stack_top;

    /* Free-running host clock; updated by ms7004_tick(). */
    uint32_t     now_ms;

    /* Host→keyboard command channel state.
     * Some commands are 2 bytes; cmd_pending stores the first byte
     * while waiting for the second.  Zero = no pending command. */
    uint8_t      cmd_pending;

    /* Keyboard configuration set by host commands. */
    bool         data_enabled;       /* data output to host (default true) */
    bool         sound_enabled;      /* bell sound enabled */
    bool         click_enabled;      /* keyclick enabled */
    bool         latin_indicator;    /* Latin indicator LED */
} ms7004_t;

/* ── API ──────────────────────────────────────────────────────────────── */

/* Initialise to reset state.  `uart` is the destination for scancode
 * bytes — typically &board->kbd.  Auto-repeat is DISABLED by default
 * (the real keyboard defaults to enabled, but we wait for either a
 * host command to turn it on or the user to opt in; this avoids
 * surprising behaviour until the host↔keyboard command set is
 * modelled). */
void ms7004_init (ms7004_t *kbd, struct ms0515_keyboard *uart);

/* Force reset: clears held state, clears toggles, disarms repeat.
 * Does NOT send anything downstream. */
void ms7004_reset(ms7004_t *kbd);

/* Press or release a physical key.  Idempotent: pressing an already-
 * held key is a no-op (no auto-repeat is produced from duplicate
 * calls — auto-repeat is driven exclusively by ms7004_tick).
 *
 * This is the single entry point for both OSK clicks and host-key
 * events; all MS 7004 behaviour (modifier latch, ALL-UP, toggles)
 * applies uniformly. */
void ms7004_key (ms7004_t *kbd, ms7004_key_t key, bool down);

/* Release every currently-held key.  Used when the emulator window
 * loses focus so that the guest does not see phantom-held keys. */
void ms7004_release_all(ms7004_t *kbd);

/* Advance internal time and emit any due auto-repeat scancode.  Call
 * once per host frame with a monotonic millisecond counter. */
void ms7004_tick(ms7004_t *kbd, uint32_t now_ms);

/* Called by the board/USART layer when the CPU writes a byte into the
 * TX buffer destined for the keyboard.  Decodes host→keyboard commands:
 * ID probe, auto-repeat enable/disable, LED control, sound/click,
 * data output enable/disable, Latin indicator, power-up reset.
 * Some commands are 2 bytes; the state machine tracks pending bytes. */
void ms7004_host_byte(ms7004_t *kbd, uint8_t byte);

/* Re-derive the live `repeat_delay_ms` / `repeat_period_ms` from the
 * (typing|game) preset and the (auto_game_mode, in_game_mode) flags.
 * Frontend calls this after editing presets or toggling auto_game_mode
 * so the next auto-repeat tick uses the new values immediately. */
void ms7004_recompute_live_repeat(ms7004_t *kbd);

/* Queries for the OSK / UI. */
bool    ms7004_caps_on  (const ms7004_t *kbd);
bool    ms7004_ruslat_on(const ms7004_t *kbd);
bool    ms7004_is_held  (const ms7004_t *kbd, ms7004_key_t key);

/* Lookup: MS 7004 scancode for a key.  Returns 0 for MS7004_KEY_NONE
 * or out-of-range values.  Exposed mainly for tests / diagnostics —
 * normal code should not need this. */
uint8_t ms7004_scancode(ms7004_key_t key);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_MS7004_H */
