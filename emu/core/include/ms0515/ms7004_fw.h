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

/* TX reassembler — watches P1[7] and assembles the bit-banged 4800-baud
 * stream into bytes.  See the .c file for the timing rationale. */
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

#define MS7004_RX_QUEUE_SIZE  16
#define MS7004_TX_HISTORY_SIZE 16

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

    /* TX reassembly state. */
    ms7004_tx_state_t tx_state;
    int               tx_cycles_to_sample;
    int               tx_bit_index;
    uint8_t           tx_byte;
    bool              tx_last_bit7;
    /* Ring of recently-assembled TX bytes (for tests).  Always written
     * regardless of whether `uart` is non-NULL. */
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

    /* Downstream UART that receives assembled scancodes.  May be
     * NULL in unit tests that only inspect tx_history. */
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

/* Enqueue a host-to-keyboard byte to be shifted out on !INT, one bit
 * per 64 CPU cycles, starting on the next ms7004_fw_run_cycles call.
 * Drops the byte if the queue is full. */
void ms7004_fw_send_host_byte(ms7004_fw_t *fw, uint8_t byte);

/* Run the CPU forward for at least `cycles` machine cycles and return
 * the actual count consumed (last instruction may overshoot by 1).
 * Drives the TX reassembler and RX bit driver in lock-step. */
int  ms7004_fw_run_cycles(ms7004_fw_t *fw, int cycles);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_MS7004_FW_H */
