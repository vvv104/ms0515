/*
 * ms7004_fw.h — firmware-driven MS 7004 keyboard backend.
 *
 * Runs the real 2 KB keyboard firmware on top of `i8035_t` + `i8243_t`,
 * with a small wrapper that maps the CPU pins to a 16x8 key matrix and
 * a 4800-baud serial line.  This is the long-term replacement for the
 * hand-rolled state machine in `ms7004.c`; the two coexist while the
 * firmware backend is being verified.
 *
 * Phase 3b (this header): just enough to load the ROM, init the CPU
 * and expander, wire callbacks, and run.  No matrix updates, no UART
 * RX/TX yet — those land in 3c.
 *
 * See docs/kb/MS7004_WIRING.md for the pin and matrix specification
 * and docs/kb/MS7004_FIRMWARE_PORT.md for the multi-session journal.
 */

#ifndef MS0515_MS7004_FW_H
#define MS0515_MS7004_FW_H

#include <stdbool.h>
#include <stdint.h>

#include <ms0515/i8035.h>
#include <ms0515/i8243.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ms0515_keyboard;     /* forward decl — see keyboard.h */

typedef struct ms7004_fw {
    i8035_t  cpu;
    i8243_t  exp;

    /* Key matrix: bit `r` of `matrix[c]` is 1 when the key at
     * column c (0..15) row r (0..7) is held.  Updated by callers
     * via ms7004_fw_press; read by the i8243 callback to compute
     * the keylatch each scan. */
    uint8_t  matrix[16];

    /* Last value latched by the most recent i8243 column write —
     * the bit of `matrix[col]` indexed by P1[0..2].  T1 returns
     * this when P1[4]=0 (STROBE asserted), 0 otherwise. */
    bool     keylatch;

    /* Downstream UART that receives assembled scancodes.  May be
     * NULL in unit tests that only inspect CPU state. */
    struct ms0515_keyboard *uart;
} ms7004_fw_t;

/* Bind to a firmware ROM image (typically 2048 bytes) and an upstream
 * UART for emitted scancodes.  Resets the CPU and expander.  The ROM
 * pointer must outlive the ms7004_fw_t (we do not copy). */
void ms7004_fw_init(ms7004_fw_t *fw,
                    const uint8_t *rom, uint16_t rom_size,
                    struct ms0515_keyboard *uart);

/* Force RESET — PC=0, A=0, all interrupts and timer disabled, expander
 * latches back to 0xF, key matrix cleared. */
void ms7004_fw_reset(ms7004_fw_t *fw);

/* Press or release a key by matrix position.  `col` must be 0..15,
 * `row` must be 0..7; out-of-range arguments are ignored.  The
 * mapping from `ms7004_key_t` to (col, row) lives in the facade
 * layer (phase 3c/3d). */
void ms7004_fw_press(ms7004_fw_t *fw, int col, int row, bool down);

/* Run the CPU forward for at least `cycles` machine cycles and return
 * the actual count consumed (last instruction may overshoot by 1).
 * Stops as soon as the cumulative cycle count reaches the target. */
int  ms7004_fw_run_cycles(ms7004_fw_t *fw, int cycles);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_MS7004_FW_H */
