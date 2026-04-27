/*
 * i8243.c — Intel 8243 4-bit I/O port expander implementation.
 *
 * See i8243.h for the wire-level protocol summary.  The model holds
 * a 4-bit latch per port (which is what's driven onto the pins) plus
 * a 4-bit "input" nibble per port that the host can use to simulate
 * external pull-downs — the effective value seen by a READ is
 * `latch & input`, matching the quasi-bidirectional behaviour of the
 * real silicon.
 */

#include <ms0515/i8243.h>

#include <string.h>

void i8243_init(i8243_t *exp)
{
    memset(exp, 0, sizeof(*exp));
    i8243_reset(exp);
}

void i8243_reset(i8243_t *exp)
{
    for (int p = 0; p < 4; p++) {
        exp->latch[p] = 0x0F;
        exp->input[p] = 0x0F;
    }
    exp->cmd          = 0;
    exp->port         = 0;
    exp->p2_low       = 0;
    exp->prog_high    = true;
    exp->driving_read = false;
}

void i8243_p2_write(i8243_t *exp, uint8_t low_nibble)
{
    exp->p2_low = (uint8_t)(low_nibble & 0x0F);
}

void i8243_prog(i8243_t *exp, bool level)
{
    if (exp->prog_high && !level) {
        /* Falling edge — latch command (bits 3..2) and port (bits 1..0)
         * from whatever the host CPU has driven on P2[3:0]. */
        exp->cmd          = (uint8_t)((exp->p2_low >> 2) & 3);
        exp->port         = (uint8_t)( exp->p2_low       & 3);
        exp->driving_read = (exp->cmd == 0);
    } else if (!exp->prog_high && level) {
        /* Rising edge — for the write-style commands, latch whatever
         * data the CPU drove on P2[3:0] during the PROG-low window. */
        switch (exp->cmd) {
        case 1: exp->latch[exp->port]  = exp->p2_low; break;  /* WRITE */
        case 2: exp->latch[exp->port] |= exp->p2_low; break;  /* ORLD  */
        case 3: exp->latch[exp->port] &= exp->p2_low; break;  /* ANLD  */
        default: break;                                        /* READ  */
        }
        exp->driving_read = false;
    }
    exp->prog_high = level;
}

uint8_t i8243_p2_read(const i8243_t *exp)
{
    if (!exp->driving_read)
        return 0x0F;                                  /* not driving */
    return (uint8_t)(exp->latch[exp->port] & exp->input[exp->port]);
}

uint8_t i8243_get_port(const i8243_t *exp, int port)
{
    return exp->latch[port & 3];
}

void i8243_set_input(i8243_t *exp, int port, uint8_t value)
{
    exp->input[port & 3] = (uint8_t)(value & 0x0F);
}
