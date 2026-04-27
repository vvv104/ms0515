/*
 * i8243.h — Intel 8243 4-bit I/O port expander.
 *
 * The 8243 adds four 4-bit I/O ports (P4..P7) to an MCS-48 system.
 * It listens to the host CPU on the low nibble of P2 plus the PROG
 * strobe and supports four commands:
 *
 *   00  READ port  — drive the latched value of the addressed port
 *                    onto P2[3:0] while PROG is low (so the CPU can
 *                    sample it via MOVD A,Pp).
 *   01  WRITE port — replace the latch on PROG's rising edge with
 *                    whatever the CPU drove on P2[3:0] in between.
 *   10  ORLD  port — OR the rising-edge value into the latch.
 *   11  ANLD  port — AND the rising-edge value into the latch.
 *
 * Wiring on the falling edge of PROG: bits [3:2] of P2 carry the
 * command, bits [1:0] carry the port number (00=P4, 01=P5, 10=P6,
 * 11=P7).  The expander then either drives or latches data while
 * PROG is held low; the rising edge ends the cycle.
 *
 * Reference: Intel 8243 datasheet (1979).  Implementation written
 * from scratch from the datasheet; MAME's i8243 served as encoding
 * cross-check only.
 */

#ifndef MS0515_I8243_H
#define MS0515_I8243_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i8243 {
    uint8_t latch[4];      /* P4..P7 — 4 output bits each            */
    uint8_t input[4];      /* host-driven input (default 0xF = none) */
    uint8_t cmd;           /* last latched command (0..3)            */
    uint8_t port;          /* last latched port number (0..3)        */
    uint8_t p2_low;        /* last 4 bits the CPU drove on P2        */
    bool    prog_high;     /* current PROG state (idles high)        */
    bool    driving_read;  /* true while a READ is in its PROG-low   */
                            /* window — controls i8243_p2_read         */
} i8243_t;

/* Bring the expander to power-on state: every output latch and every
 * host-injected input nibble defaulted to 0xF (all-high quasi-bidi). */
void i8243_init (i8243_t *exp);
void i8243_reset(i8243_t *exp);

/* CPU-side wiring — the i8035 calls these as part of MOVD / ANLD /
 * ORLD instructions. */
void    i8243_p2_write(i8243_t *exp, uint8_t low_nibble);
void    i8243_prog    (i8243_t *exp, bool level);

/* Sample what the expander is currently driving onto P2[3:0].  During
 * the PROG-low window of a READ command this returns the addressed
 * port's effective pin value; otherwise 0xF (open / not driving). */
uint8_t i8243_p2_read (const i8243_t *exp);

/* Host-side accessors. */
uint8_t i8243_get_port (const i8243_t *exp, int port);
void    i8243_set_input(i8243_t *exp, int port, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_I8243_H */
