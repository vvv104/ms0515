/*
 * timer.c — Intel 8253 / KR580VI53 Programmable Interval Timer
 *
 * Implements all 6 operating modes of the 8253 PIT.
 * See timer.h for register addresses and channel assignments.
 *
 * Sources:
 *   - Intel 8253/8254 datasheet (Order Number 231164-005)
 *   - OSDev Wiki: Programmable Interval Timer
 *   - NS4 technical description, section 4.9
 */

#include <ms0515/timer.h>
#include <string.h>

/* ── Channel initialization ───────────────────────────────────────────────── */

static void channel_init(timer_channel_t *ch)
{
    memset(ch, 0, sizeof(*ch));
    ch->gate         = true;   /* GATE defaults to high */
    ch->out          = true;   /* OUT starts high in most modes */
    ch->write_lsb_next = true;
    ch->read_lsb_next  = true;
}

void timer_init(ms0515_timer_t *timer)
{
    for (int i = 0; i < 3; i++)
        channel_init(&timer->ch[i]);
}

void timer_reset(ms0515_timer_t *timer)
{
    timer_init(timer);
}

/* ── Tick one clock cycle ─────────────────────────────────────────────────── */

static void channel_tick(timer_channel_t *ch)
{
    if (!ch->counting || !ch->loaded)
        return;

    /* GATE must be high for counting in modes 0, 2, 3, 4 */
    if (!ch->gate && ch->mode != 1 && ch->mode != 5)
        return;

    switch (ch->mode) {
    case 0:
        /* Mode 0: Interrupt on terminal count.
         * OUT goes low after control word write, remains low until
         * counter reaches zero, then goes high. */
        ch->count--;
        if (ch->count == 0)
            ch->out = true;
        break;

    case 1:
        /* Mode 1: Programmable one-shot.
         * Triggered by GATE rising edge. OUT goes low for N counts. */
        ch->count--;
        if (ch->count == 0) {
            ch->out      = true;
            ch->counting = false;
        }
        break;

    case 2:
        /* Mode 2: Rate generator.
         * OUT high for N-1 counts, low for 1 count, then reloads. */
        ch->count--;
        if (ch->count == 1) {
            ch->out = false;
        } else if (ch->count == 0) {
            ch->out   = true;
            ch->count = ch->reload;
        }
        break;

    case 3:
        /* Mode 3: Square wave generator.
         * OUT high for ceil(N/2), low for floor(N/2).
         * Decrements by 2 each tick. */
        ch->count -= 2;
        if (ch->count == 0 || ch->count == 1) {
            ch->out   = !ch->out;
            ch->count = ch->reload;
        }
        break;

    case 4:
        /* Mode 4: Software-triggered strobe.
         * OUT high, pulses low for one cycle when count reaches zero. */
        ch->count--;
        if (ch->count == 0) {
            ch->out      = false;  /* One-cycle pulse */
            ch->counting = false;
        } else if (!ch->out) {
            ch->out = true;        /* Pulse ends */
        }
        break;

    case 5:
        /* Mode 5: Hardware-triggered strobe.
         * Same as mode 4 but triggered by GATE rising edge. */
        ch->count--;
        if (ch->count == 0) {
            ch->out      = false;
            ch->counting = false;
        } else if (!ch->out) {
            ch->out = true;
        }
        break;
    }
}

void timer_tick(ms0515_timer_t *timer)
{
    for (int i = 0; i < 3; i++)
        channel_tick(&timer->ch[i]);
}

/* ── Write to timer register ──────────────────────────────────────────────── */

void timer_write(ms0515_timer_t *timer, int reg, uint8_t value)
{
    if (reg == 3) {
        /* Control word register */
        uint8_t ch_num = (value >> 6) & 3;
        if (ch_num == 3)
            return;  /* Read-back command (8254 only), ignore on 8253 */

        timer_channel_t *ch = &timer->ch[ch_num];
        uint8_t rw = (value >> 4) & 3;

        if (rw == 0) {
            /* Counter latch command: snapshot current count */
            ch->latch   = ch->count;
            ch->latched = true;
            ch->read_lsb_next = true;
            return;
        }

        /* Program channel mode */
        ch->mode    = (value >> 1) & 7;
        if (ch->mode > 5) ch->mode &= 3;  /* Modes 6,7 → 2,3 */
        ch->bcd     = (value & 1) != 0;
        ch->rw_mode = rw;
        ch->write_lsb_next = (rw == 1 || rw == 3);
        ch->read_lsb_next  = (rw == 1 || rw == 3);
        ch->counting = false;
        ch->loaded   = false;

        /* OUT initial state depends on mode */
        if (ch->mode == 0)
            ch->out = false;   /* Mode 0: OUT starts low */
        else
            ch->out = true;    /* All other modes: OUT starts high */
    } else {
        /* Data register write (channel 0, 1, or 2) */
        timer_channel_t *ch = &timer->ch[reg];

        switch (ch->rw_mode) {
        case 1:  /* LSB only */
            ch->reload = value;
            ch->count  = value;
            ch->loaded = true;
            ch->counting = true;
            break;

        case 2:  /* MSB only */
            ch->reload = (uint16_t)value << 8;
            ch->count  = ch->reload;
            ch->loaded = true;
            ch->counting = true;
            break;

        case 3:  /* LSB then MSB */
            if (ch->write_lsb_next) {
                ch->reload = (ch->reload & 0xFF00) | value;
                ch->write_lsb_next = false;
            } else {
                ch->reload = (ch->reload & 0x00FF) | ((uint16_t)value << 8);
                ch->count  = ch->reload;
                ch->loaded = true;
                ch->counting = true;
                ch->write_lsb_next = true;
            }
            break;
        }
    }
}

/* ── Read from timer register ─────────────────────────────────────────────── */

uint8_t timer_read(ms0515_timer_t *timer, int reg)
{
    if (reg == 3)
        return 0xFF;  /* Control register is write-only */

    timer_channel_t *ch = &timer->ch[reg];
    uint16_t val = ch->latched ? ch->latch : ch->count;

    switch (ch->rw_mode) {
    case 1:  /* LSB only */
        ch->latched = false;
        return (uint8_t)(val & 0xFF);

    case 2:  /* MSB only */
        ch->latched = false;
        return (uint8_t)(val >> 8);

    case 3:  /* LSB then MSB */
        if (ch->read_lsb_next) {
            ch->read_lsb_next = false;
            return (uint8_t)(val & 0xFF);
        } else {
            ch->read_lsb_next = true;
            ch->latched = false;
            return (uint8_t)(val >> 8);
        }

    default:
        return 0;
    }
}

/* ── GATE signal control ──────────────────────────────────────────────────── */

void timer_set_gate(ms0515_timer_t *timer, int channel, bool state)
{
    if (channel < 0 || channel > 2)
        return;

    timer_channel_t *ch = &timer->ch[channel];
    bool rising_edge = !ch->gate && state;
    ch->gate = state;

    if (rising_edge) {
        switch (ch->mode) {
        case 1:
        case 5:
            /* Rising GATE triggers / restarts counting */
            ch->count    = ch->reload;
            ch->out      = false;
            ch->counting = true;
            break;
        case 2:
        case 3:
            /* Rising GATE reloads and restarts */
            ch->count    = ch->reload;
            ch->counting = true;
            break;
        default:
            break;
        }
    }

    /* GATE going low in modes 2, 3 forces OUT high */
    if (!state && (ch->mode == 2 || ch->mode == 3))
        ch->out = true;
}

bool timer_get_out(const ms0515_timer_t *timer, int channel)
{
    if (channel < 0 || channel > 2)
        return false;
    return timer->ch[channel].out;
}
