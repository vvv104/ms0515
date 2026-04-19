/*
 * keyboard.c — MS7004 keyboard interface via i8251 USART
 *
 * Implements the KR580VV51 (Intel 8251 clone) USART that connects the
 * CPU to the Elektronika MS 7004 keyboard over a serial link.
 *
 * The i8251 has a two-phase programming sequence after reset:
 *   Phase 0 — mode instruction (baud rate, character length, parity, stop bits)
 *   Phase 1 — command instruction (Tx/Rx enable, error reset, RTS/DTR)
 *
 * After the initial programming, writes to the command/mode address (reg 1)
 * go directly to the command register.  An internal reset (command bit 6)
 * returns the chip to phase 0 so a new mode instruction can be loaded.
 *
 * The MS0515 BIOS sends three zero bytes before the reset command to
 * force the chip into a known state, regardless of what phase it was in.
 *
 * Sources:
 *   - Intel 8251 USART datasheet (Order Number AFN-01819B)
 *   - NS4 technical description, section 4.10.1
 */

#include <ms0515/keyboard.h>
#include <string.h>

/* ── i8251 command register bits ─────────────────────────────────────────── */

#define CMD_TXEN    0x01   /* Transmit enable                                */
#define CMD_DTR     0x02   /* Data Terminal Ready                            */
#define CMD_RXEN    0x04   /* Receive enable                                 */
#define CMD_SBRK    0x08   /* Send break character                           */
#define CMD_ERESET  0x10   /* Error reset (clears PE, OE, FE)               */
#define CMD_RTS     0x20   /* Request To Send                                */
#define CMD_IRESET  0x40   /* Internal reset (returns to mode phase)         */
#define CMD_HUNT    0x80   /* Hunt mode (sync only, not used here)           */

/* ── Initialization ──────────────────────────────────────────────────────── */

void kbd_init(ms0515_keyboard_t *kbd)
{
    /* Preserve the tx_callback across reset — it is set once by the
     * Emulator and must survive board_reset() → kbd_reset() cycles. */
    void (*cb)(void *, uint8_t) = kbd->tx_callback;
    void  *ctx                  = kbd->tx_callback_ctx;

    memset(kbd, 0, sizeof(*kbd));

    kbd->tx_callback     = cb;
    kbd->tx_callback_ctx = ctx;
    kbd->tx_ready  = true;
    kbd->status    = KBD_STATUS_TXRDY | KBD_STATUS_TXEMPTY;
    kbd->init_step = 0;
}

void kbd_reset(ms0515_keyboard_t *kbd)
{
    kbd_init(kbd);
}

/* ── FIFO helpers ────────────────────────────────────────────────────────── */

static bool fifo_empty(const ms0515_keyboard_t *kbd)
{
    return kbd->fifo_count == 0;
}

static bool fifo_full(const ms0515_keyboard_t *kbd)
{
    return kbd->fifo_count >= 16;
}

static void fifo_push(ms0515_keyboard_t *kbd, uint8_t byte)
{
    if (fifo_full(kbd))
        return;

    kbd->fifo[kbd->fifo_tail] = byte;
    kbd->fifo_tail = (kbd->fifo_tail + 1) & 15;
    kbd->fifo_count++;
}

static uint8_t fifo_pop(ms0515_keyboard_t *kbd)
{
    if (fifo_empty(kbd))
        return 0;

    uint8_t byte = kbd->fifo[kbd->fifo_head];
    kbd->fifo_head = (kbd->fifo_head + 1) & 15;
    kbd->fifo_count--;
    return byte;
}

/* ── Update status register and IRQ line ─────────────────────────────────── */

static void update_status(ms0515_keyboard_t *kbd)
{
    /* RXRDY — data waiting in the receiver buffer */
    if (kbd->rx_ready)
        kbd->status |= KBD_STATUS_RXRDY;
    else
        kbd->status &= ~KBD_STATUS_RXRDY;

    /* TXRDY — transmitter can accept a byte (and TxEN is set) */
    if (kbd->tx_ready && (kbd->command & CMD_TXEN))
        kbd->status |= KBD_STATUS_TXRDY;
    else
        kbd->status &= ~KBD_STATUS_TXRDY;

    /* TXEMPTY — always set since we don't model real serial output timing */
    kbd->status |= KBD_STATUS_TXEMPTY;

    /* Assert IRQ when receiver has data and RxEN is set */
    kbd->irq = kbd->rx_ready && (kbd->command & CMD_RXEN);
}

/* ── CPU register access ─────────────────────────────────────────────────── */

void kbd_write(ms0515_keyboard_t *kbd, int reg, uint8_t value)
{
    if (reg == 0) {
        /*
         * Data register write — CPU sends a byte to the keyboard.
         *
         * The byte is forwarded to the ms7004 keyboard model via the
         * tx_callback, which handles LK201-style commands (probe,
         * division modes, auto-repeat, etc.).
         */
        kbd->tx_data  = value;
        kbd->tx_ready = true;

        /* Forward the byte to the ms7004 keyboard model. */
        if (kbd->tx_callback)
            kbd->tx_callback(kbd->tx_callback_ctx, value);

        update_status(kbd);
        return;
    }

    /* reg == 1: command / mode register */

    if (kbd->init_step == 0) {
        /*
         * Phase 0 — mode instruction.
         *
         * The BIOS writes three zero bytes before a real mode word.
         * A zero mode instruction is harmless, so we just store it
         * and advance to phase 1 on any non-zero value.
         *
         * Mode word format (async):
         *   bits 1:0 — baud rate factor (01=1x, 10=16x, 11=64x)
         *   bits 3:2 — character length (00=5, 01=6, 10=7, 11=8)
         *   bit  4   — parity enable
         *   bit  5   — parity type (0=odd, 1=even)
         *   bits 7:6 — stop bits (01=1, 10=1.5, 11=2)
         *
         * A zero byte has baud factor = 00, which means "sync mode".
         * The BIOS relies on this being a no-op so it can flush the
         * chip's unknown state before issuing the real reset command.
         */
        kbd->mode = value;

        /* Only advance to phase 1 if baud factor is non-zero (async) */
        if ((value & 0x03) != 0)
            kbd->init_step = 1;

        return;
    }

    /*
     * Phase 1 (and beyond) — command instruction.
     */
    kbd->command = value;

    /* Internal reset returns chip to phase 0 */
    if (value & CMD_IRESET) {
        kbd->init_step = 0;
        kbd->command   = 0;
        kbd->rx_ready  = false;
        kbd->tx_ready  = true;
        update_status(kbd);
        return;
    }

    /* Error reset clears error flags */
    if (value & CMD_ERESET) {
        kbd->status &= ~(KBD_STATUS_PE | KBD_STATUS_OE | KBD_STATUS_FE);
    }

    update_status(kbd);
}

uint8_t kbd_read(ms0515_keyboard_t *kbd, int reg)
{
    if (reg == 0) {
        /*
         * Data register read — CPU reads the received byte.
         * Clears RXRDY; next byte will be loaded from FIFO by kbd_tick().
         */
        uint8_t data = kbd->rx_data;
        kbd->rx_ready = false;
        update_status(kbd);
        return data;
    }

    /* reg == 1: status register (read-only) */
    return kbd->status;
}

/* ── Host interface ──────────────────────────────────────────────────────── */

void kbd_push_scancode(ms0515_keyboard_t *kbd, uint8_t scancode)
{
    fifo_push(kbd, scancode);
}

void kbd_flush_fifo(ms0515_keyboard_t *kbd)
{
    kbd->fifo_head  = 0;
    kbd->fifo_tail  = 0;
    kbd->fifo_count = 0;
    kbd->rx_ready   = false;
    update_status(kbd);
}

/* ── Clock tick ──────────────────────────────────────────────────────────── */

void kbd_tick(ms0515_keyboard_t *kbd)
{
    /*
     * Transfer the next byte from the FIFO into the receiver buffer
     * when the buffer is empty and RxEN is set.
     *
     * In real hardware this happens at the baud rate (4800 baud,
     * clocked by PIT channel 0).  For emulation we transfer one
     * byte per tick call — the caller controls the effective rate
     * by choosing how often to call kbd_tick().
     */
    if (!kbd->rx_ready && !fifo_empty(kbd)) {
        kbd->rx_data  = fifo_pop(kbd);
        kbd->rx_ready = true;

        /* Check for overrun — if previous data wasn't read */
        /* (Already cleared by reading, so this path means rapid input) */
    }

    update_status(kbd);
}
