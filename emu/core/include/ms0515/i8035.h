/*
 * i8035.h — Intel MCS-48 (8035 / 8048 family) CPU core.
 *
 * The 8035 is the ROM-less variant of the 8048: same instruction set,
 * same 64 bytes of internal RAM, but program memory is supplied
 * externally as a blob (up to 4 KB across two MB-selectable banks).
 *
 * This implementation targets exactly what the MS 7004 keyboard needs:
 * 8035 CPU running a 2 KB external ROM image, talking to an i8243
 * port expander.  The i8049-only opcodes (RAD, MOVP3 to bank 1, etc.)
 * are not implemented — the `step()` function asserts on those.
 *
 * Pin model (callback-based):
 *   • Three 8-bit ports (BUS, P1, P2) via port_read / port_write.
 *   • Three input pins (T0, T1, INT) via separate read functions —
 *     INT is active-low; the implementation calls read_int and treats
 *     a `true` return as "INT line held low".
 *   • One output pin (PROG) used to strobe the i8243; called on each
 *     edge during MOVD / ANLD / ORLD.
 *
 * Reference: Intel MCS-48 Microcomputer User's Manual / Programmer's
 * Reference (Intel, 1976).  The encoding cross-checked against MAME's
 * i8x4x source served as documentation only — code is original.
 */

#ifndef MS0515_I8035_H
#define MS0515_I8035_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Port / pin callback types ────────────────────────────────────────── */

typedef uint8_t (*i8035_port_read_fn) (void *ctx, uint8_t port);
typedef void    (*i8035_port_write_fn)(void *ctx, uint8_t port, uint8_t val);
typedef bool    (*i8035_pin_read_fn)  (void *ctx);
typedef void    (*i8035_prog_fn)      (void *ctx, bool level);

/* Port identifiers passed to port_read / port_write. */
#define I8035_PORT_BUS  0
#define I8035_PORT_P1   1
#define I8035_PORT_P2   2

/* ── Architectural state ──────────────────────────────────────────────── */

typedef struct i8035 {
    /* Program counter: 12 bits (4 KB).  The MB bit selects between
     * 0..2047 and 2048..4095 for long jumps / calls; sequential
     * fetches wrap inside the current page. */
    uint16_t pc;

    /* Accumulator. */
    uint8_t  a;

    /* PSW layout: CY:AC:F0:BS:1:SP[2:0].
     *   bit 7 — CY  carry from add / rotate
     *   bit 6 — AC  auxiliary carry from bit 3 to 4 (for DA A)
     *   bit 5 — F0  user flag 0 (also CLR/CPL F0 modify it)
     *   bit 4 — BS  register bank select (0 = RB0, 1 = RB1)
     *   bit 3 — 1   reads as one
     *   bits 2..0 — stack pointer (0..7) */
    uint8_t  psw;

    /* 64 bytes of internal RAM.
     *   [0..7]   R0..R7 of bank 0
     *   [8..23]  stack (8 levels × 2 bytes)
     *   [24..31] R0..R7 of bank 1
     *   [32..63] general-purpose RAM */
    uint8_t  ram[64];

    /* Timer / counter register, accessible via MOV T,A and MOV A,T. */
    uint8_t  t;

    /* Output latches for P1 and P2.  When the firmware does
     * OUTL Pn,A the latch is overwritten; ANL/ORL Pn,#imm modify it
     * in place.  Actual host-visible value is the latch (no input
     * pull-down on the simulated pins). */
    uint8_t  p1_out;
    uint8_t  p2_out;

    /* User flag F1, separate from PSW.F0; toggled by CLR F1 / CPL F1
     * and tested by JF1.  Not part of PSW, not pushed by interrupts. */
    bool     f1;

    /* Timer overflow flag.  Set when the timer counter rolls FF→00
     * (timer or counter mode), cleared by JTF or RESET.  Drives the
     * timer/counter interrupt when TIE is enabled. */
    bool     tf;

    /* Interrupt enables. */
    bool     ie;        /* external INT pin enable (EN I / DIS I) */
    bool     tie;       /* timer/counter interrupt enable (EN/DIS TCNTI) */
    bool     in_irq;    /* currently inside an interrupt handler */

    /* Timer / counter run state. */
    bool     timer_run;     /* STRT T started (prescaled by 32) */
    bool     counter_run;   /* STRT CNT started (counts T1 falling edges) */
    uint8_t  prescaler;     /* 5-bit prescaler for timer mode (0..31) */
    bool     last_t1;       /* sampled T1 for falling-edge detect */

    /* Memory bank select for 8049-style long jump.  Only meaningful
     * when ROM > 2048 bytes; the ms7004 firmware is exactly 2048
     * bytes and never sets this. */
    bool     mb;

    /* External program ROM. */
    const uint8_t *rom;
    uint16_t       rom_size;

    /* Host wiring. */
    void                *host_ctx;
    i8035_port_read_fn   port_read;
    i8035_port_write_fn  port_write;
    i8035_pin_read_fn    read_t0;
    i8035_pin_read_fn    read_t1;
    i8035_pin_read_fn    read_int;
    i8035_prog_fn        prog;
} i8035_t;

/* ── API ──────────────────────────────────────────────────────────────── */

/* Bind the CPU to a ROM image and host callbacks.  Does NOT reset; call
 * i8035_reset afterwards.  Any callback may be NULL — port reads then
 * return 0xFF, port writes are dropped, pin reads return false. */
void i8035_init(i8035_t *cpu,
                const uint8_t *rom, uint16_t rom_size,
                void *host_ctx,
                i8035_port_read_fn  port_read,
                i8035_port_write_fn port_write,
                i8035_pin_read_fn   read_t0,
                i8035_pin_read_fn   read_t1,
                i8035_pin_read_fn   read_int,
                i8035_prog_fn       prog);

/* Force RESET: PC=0, A=0, PSW=0x08, P1=P2=0xFF, all interrupts and
 * timer disabled, F0/F1=0, TF=0, MB=0, register bank 0 selected,
 * stack pointer = 0. */
void i8035_reset(i8035_t *cpu);

/* Execute one instruction.  Returns the number of machine cycles the
 * instruction consumed (1 or 2 — one machine cycle = 15 oscillator
 * periods on the real chip; for the ms7004 at 4.608 MHz this is
 * roughly 3.26 µs per cycle).  Drives the timer/counter prescaler
 * forward and services pending interrupts before fetching. */
int i8035_step(i8035_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_I8035_H */
