/*
 * timer.h — Intel 8253 / KR580VI53 Programmable Interval Timer
 *
 * The MS0515 uses a KR580VI53 (Soviet clone of Intel 8253 PIT) with
 * three independent 16-bit down-counters.  Each channel has GATE, CLK,
 * and OUT signals.
 *
 * Channel assignment on the MS0515 system board:
 *   Channel 0  Serial port baud rate generator (keyboard UART clock)
 *              Programmed to mode 3, divisor 26 (decimal) at 2 MHz
 *              → output 76923 Hz ≈ 4800 × 16 baud
 *   Channel 1  Serial port baud rate generator (printer UART clock)
 *   Channel 2  Speaker tone generator / time interval counter
 *              Output gated by System Register C bits 6-7
 *
 * Register addresses on the MS0515 bus:
 *   Read:   0177500 (ch0), 0177502 (ch1), 0177504 (ch2), 0177506 (ctrl)
 *   Write:  0177520 (ch0), 0177522 (ch1), 0177524 (ch2), 0177526 (ctrl)
 *
 * Timer clock frequency: 2 MHz (500 ns period)
 *
 * Control word format (8 bits):
 *   Bits 7-6  Channel select:  00=ch0, 01=ch1, 10=ch2, 11=readback(8254)
 *   Bits 5-4  R/W mode:  00=latch, 01=LSB only, 10=MSB only, 11=LSB+MSB
 *   Bits 3-1  Operating mode (0–5)
 *   Bit  0    Count format: 0=binary (0–65535), 1=BCD (0–9999)
 *
 * Operating modes:
 *   Mode 0  Interrupt on terminal count — OUT low until count reaches 0
 *   Mode 1  Programmable one-shot — triggered by GATE rising edge
 *   Mode 2  Rate generator — divide-by-N, one-cycle low pulse
 *   Mode 3  Square wave — symmetric high/low output
 *   Mode 4  Software-triggered strobe
 *   Mode 5  Hardware-triggered strobe
 *
 * Sources:
 *   - Intel 8253/8254 datasheet
 *   - NS4 technical description, section 4.9
 *   - OSDev Wiki: Programmable Interval Timer
 */

#ifndef MS0515_TIMER_H
#define MS0515_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Timer channel state ──────────────────────────────────────────────────── */

typedef struct {
    uint16_t count;             /* Current counter value                     */
    uint16_t reload;            /* Reload (initial count) value              */
    uint16_t latch;             /* Latched value for read-back               */
    bool     latched;           /* True if latch command was issued          */

    uint8_t  mode;              /* Operating mode (0–5)                      */
    uint8_t  rw_mode;           /* R/W mode: 1=LSB, 2=MSB, 3=LSB then MSB   */
    bool     bcd;               /* True for BCD counting                     */

    bool     write_lsb_next;    /* For rw_mode==3: next write is LSB         */
    bool     read_lsb_next;     /* For rw_mode==3: next read is LSB          */

    bool     gate;              /* GATE input state                          */
    bool     out;               /* OUT output state                          */
    bool     counting;          /* True if counter is actively counting      */
    bool     loaded;            /* True if count value has been loaded       */
} timer_channel_t;

/* ── Timer (3 channels) ──────────────────────────────────────────────────── */

typedef struct ms0515_timer {
    timer_channel_t ch[3];
} ms0515_timer_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

void     timer_init(ms0515_timer_t *timer);
void     timer_reset(ms0515_timer_t *timer);

/*
 * timer_tick — Advance all channels by one clock cycle.
 *
 * Call this once per timer clock period (500 ns = 2 MHz).
 * Updates counters, evaluates modes, sets OUT states.
 */
void     timer_tick(ms0515_timer_t *timer);

/*
 * timer_write — Write a byte to a timer register.
 *   `reg` = 0..2 for channel data, 3 for control word.
 */
void     timer_write(ms0515_timer_t *timer, int reg, uint8_t value);

/*
 * timer_read — Read a byte from a timer register.
 *   `reg` = 0..2 for channel data, 3 for control word (returns 0xFF).
 */
uint8_t  timer_read(ms0515_timer_t *timer, int reg);

/*
 * timer_set_gate — Set the GATE input for a channel.
 */
void     timer_set_gate(ms0515_timer_t *timer, int channel, bool state);

/*
 * timer_get_out — Read the OUT signal for a channel.
 */
bool     timer_get_out(const ms0515_timer_t *timer, int channel);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_TIMER_H */
