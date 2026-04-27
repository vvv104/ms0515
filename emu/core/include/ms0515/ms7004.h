/*
 * ms7004.h — Elektronika MS 7004 keyboard microcontroller model.
 *
 * The MS 7004 is a serial keyboard derived from the DEC LK201.  An
 * Intel 8035 microcontroller inside the keyboard scans the key matrix,
 * tracks modifier state, handles auto-repeat, and emits 8-bit
 * scancodes over a 4800-baud serial line into the host's i8251 USART.
 *
 * This module runs the real keyboard firmware on top of i8035 + i8243
 * cores (see i8035.h, i8243.h, docs/kb/MS7004_WIRING.md).  Callers
 * push physical key presses by enum or matrix position; the firmware
 * takes care of scanning, debouncing, ALL-UP, repeat, and command
 * decoding, just like the real chip.
 *
 * Design summary
 * --------------
 * `core/keyboard.c` is a pure i8251 USART — it does not know what
 * bytes mean.  This module sits logically on the serial line's *other*
 * end: the frontend tells it "physical key X went down", and the
 * firmware produces the scancode sequence the real MS 7004 would have
 * emitted, pushing bytes into the USART's RX FIFO.  Host commands
 * (the USART's TX side) flow back into `ms7004_host_byte`, which feeds
 * the firmware's external IRQ pin one bit at a time.
 *
 * Sources
 * -------
 *   - `mc7004_keyboard_original.rom` (2 KB, CRC32 69fcab53) — the
 *     authoritative state machine, run as actual MCS-48 code.
 *   - MAME `src/mame/shared/ms7004.cpp` — pin and matrix wiring
 *     reference (data only, code original).
 *   - DEC LK201 Technical Manual (EK-104AA-TM) — protocol overview.
 */

#ifndef MS0515_MS7004_H
#define MS0515_MS7004_H

#include <stdbool.h>
#include <stdint.h>

#include <ms0515/i8035.h>
#include <ms0515/i8243.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ms0515_keyboard;     /* forward decl — see keyboard.h */

/*
 * Physical key identifiers.  Each enum value corresponds to ONE cap on
 * the real MS 7004.  Values are opaque indices; the matrix-position
 * mapping lives inside ms7004.c (kKeyMatrix[]).
 *
 * Ordering below follows the physical layout (top strip, then main
 * block row-by-row, then editing cluster / arrows / PF / numpad).
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
    MS7004_KEY_CTRL,            /* СУ    sc 0o257 */
    MS7004_KEY_CAPS,            /* ФКС   sc 0o260 (toggle) */
    MS7004_KEY_F, MS7004_KEY_Y, MS7004_KEY_W, MS7004_KEY_A,
    MS7004_KEY_P, MS7004_KEY_R, MS7004_KEY_O, MS7004_KEY_L,
    MS7004_KEY_D,
    MS7004_KEY_V,               /* "Ж/V"  sc 0o362 */
    MS7004_KEY_BACKSLASH,       /* "Э\"   sc 0o373 */
    MS7004_KEY_PERIOD,          /* ".>"   sc 0o367 */
    MS7004_KEY_HARDSIGN,        /* Ъ      sc 0o361 */

    /* ── Row 4: bottom letter row ────────────────────────────────── */
    MS7004_KEY_SHIFT_L,         /* ВР left  sc 0o256 */
    MS7004_KEY_RUSLAT,          /* РУС/ЛАТ  sc 0o262 (toggle) */
    MS7004_KEY_Q,               /* "Я/Q"  sc 0o303 */
    MS7004_KEY_CHE,             /* "Ч/¬"  sc 0o310 */
    MS7004_KEY_S, MS7004_KEY_M, MS7004_KEY_I, MS7004_KEY_T,
    MS7004_KEY_X,               /* "Ь/X"  sc 0o343 */
    MS7004_KEY_B,
    MS7004_KEY_AT,              /* "Ю/@"  sc 0o355 */
    MS7004_KEY_COMMA,           /* ",<"   sc 0o363 */
    MS7004_KEY_SLASH,           /* "/?"   sc 0o312 */
    MS7004_KEY_UNDERSCORE,      /* "_"    sc 0o361 */
    MS7004_KEY_SHIFT_R,         /* ВР right — shares scancode 0o256 */

    /* ── Row 5: bottom row (space etc.) ──────────────────────────── */
    MS7004_KEY_COMPOSE,         /* КМП    sc 0o261 */
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

/* TX reassembler — watches P1[7] and assembles the bit-banged
 * 4800-baud stream into bytes.  See ms7004.c for the timing rationale. */
typedef enum {
    MS7004_TX_IDLE,        /* line is high, waiting for start bit       */
    MS7004_TX_SAMPLING,    /* start bit seen, sampling 8 data bits      */
    MS7004_TX_STOP_CHECK,  /* validating that the stop bit is high      */
} ms7004_tx_state_t;

/* RX driver — drives the !INT pin from a queue of host-to-keyboard
 * bytes, one bit per 64 CPU cycles. */
typedef enum {
    MS7004_RX_IDLE,        /* nothing to send, INT released (high)      */
    MS7004_RX_START,       /* driving INT low for the start bit         */
    MS7004_RX_DATA,        /* driving INT for one of 8 data bits        */
    MS7004_RX_STOP,        /* releasing INT high for the stop bit       */
} ms7004_rx_state_t;

#define MS7004_RX_QUEUE_SIZE   16
#define MS7004_TX_HISTORY_SIZE 16

/* ── Public state ─────────────────────────────────────────────────────── */

typedef struct ms7004 {
    /* Keyboard firmware running on i8035 + i8243.  Drives all real
     * keyboard behaviour — matrix scan, ALL-UP, auto-repeat, modifier
     * latching, host command decoding. */
    i8035_t  cpu;
    i8243_t  exp;

    /* Key matrix: bit `r` of `matrix[c]` is 1 when the key at
     * column c (0..15), row r (0..7) is held.  Updated by the public
     * ms7004_key / ms7004_release_all entry points. */
    uint8_t  matrix[16];

    /* Last value latched by the most recent i8243 column write.  T1
     * returns this when P1[4]=0 (STROBE asserted), 0 otherwise. */
    bool     keylatch;

    /* TX reassembly state. */
    ms7004_tx_state_t tx_state;
    int               tx_cycles_to_sample;
    int               tx_bit_index;
    uint8_t           tx_byte;
    bool              tx_last_bit7;
    /* Ring of recently-assembled TX bytes (for tests).  Always written. */
    uint8_t           tx_history[MS7004_TX_HISTORY_SIZE];
    int               tx_history_count;

    /* RX driver state. */
    ms7004_rx_state_t rx_state;
    int               rx_cycles_to_event;
    int               rx_bit_index;
    uint8_t           rx_byte;
    bool              rx_int_low;        /* active-low: true = INT asserted */
    uint8_t           rx_queue[MS7004_RX_QUEUE_SIZE];
    int               rx_q_head, rx_q_tail;

    /* Toggle states observed from the firmware's TX byte stream:
     * each emission of CAPS scancode (0o260) flips caps_on, each
     * RUSLAT scancode (0o262) flips ruslat_on.  The frontend reads
     * these to display modifier state on the on-screen keyboard. */
    bool     caps_on;
    bool     ruslat_on;

    /* Wall-clock tracking for ms7004_tick → cycle conversion. */
    uint32_t last_tick_ms;
    bool     last_tick_valid;

    /* Firmware ROM blob (attached via ms7004_attach_firmware). */
    const uint8_t *firmware_rom;
    uint16_t       firmware_rom_size;

    /* Downstream USART that receives assembled scancode bytes. */
    struct ms0515_keyboard *uart;
} ms7004_t;

/* ── API ──────────────────────────────────────────────────────────────── */

/* Initialise to the post-power-on state.  `uart` is the destination
 * for scancode bytes — typically &board->kbd.  After init you MUST
 * call ms7004_attach_firmware before any keyboard interaction; without
 * the firmware ROM the i8035 has nothing to execute. */
void ms7004_init(ms7004_t *kbd, struct ms0515_keyboard *uart);

/* Attach the keyboard firmware ROM blob (typically 2048 bytes, the
 * mc7004_keyboard_original.rom asset).  The pointer is stored, not
 * copied — `rom` must outlive `kbd`.  Resets the i8035 so it begins
 * executing from the ROM's reset vector at PC=0. */
void ms7004_attach_firmware(ms7004_t *kbd,
                            const uint8_t *rom, uint16_t rom_size);

/* Force RESET: re-initialise all state without disturbing the
 * firmware ROM binding or the UART pointer.  The CPU restarts at
 * PC=0 just as on power-up. */
void ms7004_reset(ms7004_t *kbd);

/* Press or release a physical key.  Idempotent: pressing an already-
 * held key is a no-op.  Out-of-range or unmapped enum values silently
 * no-op (some caps in the enum exist for OSK display only and have
 * no matrix position on the real keyboard). */
void ms7004_key(ms7004_t *kbd, ms7004_key_t key, bool down);

/* Release every currently-held key.  Used when the emulator window
 * loses focus so the firmware does not see phantom-held keys. */
void ms7004_release_all(ms7004_t *kbd);

/* Advance internal time.  Converts the elapsed delta since the
 * previous call into i8035 machine cycles and runs the firmware that
 * far forward.  At 4.608 MHz / 15 = 307 200 inst/s, one 16 ms host
 * frame is ≈4920 instructions. */
void ms7004_tick(ms7004_t *kbd, uint32_t now_ms);

/* Push a host-to-keyboard byte (typically from the i8251 USART TX)
 * into the firmware's external IRQ line.  The byte is queued and
 * shifted out on !INT one bit per 64 CPU cycles starting on the next
 * ms7004_tick / run call. */
void ms7004_host_byte(ms7004_t *kbd, uint8_t byte);

/* Queries for the OSK / UI. */
bool    ms7004_caps_on  (const ms7004_t *kbd);
bool    ms7004_ruslat_on(const ms7004_t *kbd);
bool    ms7004_is_held  (const ms7004_t *kbd, ms7004_key_t key);

/* Lookup: canonical MS 7004 scancode for a key.  Returns 0 for
 * MS7004_KEY_NONE / out-of-range / caps with no matrix position.
 * This is a pure data lookup (LK201-derived); the firmware's actual
 * emission may differ for modifier-affected keys, but for plain
 * make-codes the values match — verified in test_ms7004.cpp. */
uint8_t ms7004_scancode(ms7004_key_t key);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_MS7004_H */
