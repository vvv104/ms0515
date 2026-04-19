/*
 * keyboard.h — MS7004 keyboard interface via i8251 USART
 *
 * The MS0515 keyboard port uses a KR580VV51 (Soviet clone of Intel 8251
 * USART) to communicate with the Elektronika MS 7004 keyboard.
 *
 * The keyboard sends 8-bit scan codes over a serial link at 4800 baud
 * (clocked by PIT8253 channel 0).
 *
 * Register addresses on the MS0515 bus:
 *   0177440  Receiver data buffer     (read)
 *   0177442  Status register          (read)  / Command register (write)
 *   0177460  Transmitter data buffer  (write)
 *
 * The i8251 generates an interrupt on IRQ5 (vector 0130, priority 5)
 * when a byte is received from the keyboard.
 *
 * Initialization sequence (from NS4 tech desc, section 4.10.1):
 *   1. Write three zero bytes to command register (reset to known state)
 *   2. Write reset command
 *   3. Write mode instruction
 *   4. Write command instruction
 *   5. USART is now ready for data exchange
 *
 * Sources:
 *   - Intel 8251 USART datasheet
 *   - NS4 technical description, section 4.10.1, Table 12
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#ifndef MS0515_KEYBOARD_H
#define MS0515_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── i8251 Status register bits ───────────────────────────────────────────── */

#define KBD_STATUS_TXRDY    0x01   /* Transmitter ready                     */
#define KBD_STATUS_RXRDY    0x02   /* Receiver ready (data available)       */
#define KBD_STATUS_TXEMPTY  0x04   /* Transmitter empty                     */
#define KBD_STATUS_PE       0x08   /* Parity error                          */
#define KBD_STATUS_OE       0x10   /* Overrun error                         */
#define KBD_STATUS_FE       0x20   /* Framing error                         */
#define KBD_STATUS_SYNDET   0x40   /* Sync detect / break detect            */
#define KBD_STATUS_DSR      0x80   /* Data Set Ready                        */

/* ── Keyboard state ───────────────────────────────────────────────────────── */

typedef struct ms0515_keyboard {
    /* i8251 internal state */
    uint8_t  rx_data;           /* Received data byte                       */
    uint8_t  tx_data;           /* Transmitted data byte                    */
    uint8_t  status;            /* Status register                          */
    uint8_t  mode;              /* Mode instruction                         */
    uint8_t  command;           /* Command instruction                      */

    bool     rx_ready;          /* Data available in rx_data                */
    bool     tx_ready;          /* Transmitter is ready for next byte       */

    /* Initialization state machine */
    int      init_step;         /* Tracks mode/command programming sequence */

    /* Input FIFO — host pushes scan codes here */
    uint8_t  fifo[16];
    int      fifo_head;
    int      fifo_tail;
    int      fifo_count;

    /* Serial transfer timing — models the real baud rate delay.
     * At 4800 baud with 11 bits/frame, one byte takes ~2.3 ms.
     * kbd_tick is called every KBD_TICK_DIVIDER*TIMER_DIVIDER CPU
     * cycles (~2048 cycles at 7.5 MHz ≈ 273 µs), so we need ~8 ticks
     * per byte to match the real serial rate. */
    int      rx_delay;          /* Ticks remaining before next FIFO→rx      */

    /* Interrupt request output */
    bool     irq;               /* True when IRQ should be asserted         */

    /* Host→keyboard command callback.  When the CPU writes a byte to the
     * TX data register, this callback forwards it to the ms7004 model.
     * Set by the Emulator during init; NULL if not wired up. */
    void (*tx_callback)(void *ctx, uint8_t byte);
    void  *tx_callback_ctx;
} ms0515_keyboard_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

void    kbd_init(ms0515_keyboard_t *kbd);
void    kbd_reset(ms0515_keyboard_t *kbd);

/*
 * kbd_write — CPU writes to a keyboard register.
 *   `reg` = 0: data register (0177460)
 *   `reg` = 1: command/mode register (0177442)
 */
void    kbd_write(ms0515_keyboard_t *kbd, int reg, uint8_t value);

/*
 * kbd_read — CPU reads from a keyboard register.
 *   `reg` = 0: data register (0177440)
 *   `reg` = 1: status register (0177442)
 */
uint8_t kbd_read(ms0515_keyboard_t *kbd, int reg);

/*
 * kbd_push_scancode — Push a scan code from the host into the FIFO.
 *
 * Called by the frontend when a key is pressed or released.
 */
void    kbd_push_scancode(ms0515_keyboard_t *kbd, uint8_t scancode);

/*
 * kbd_flush_fifo — Discard all pending bytes in the receive FIFO.
 *
 * Used by the keyboard model before pushing a probe/reset response,
 * so stale auto-repeat data doesn't block the response.
 */
void    kbd_flush_fifo(ms0515_keyboard_t *kbd);

/*
 * kbd_tick — Advance the keyboard UART state.
 *
 * Should be called at the UART clock rate.  Transfers bytes from FIFO
 * to the receiver buffer when the receiver is idle.
 */
void    kbd_tick(ms0515_keyboard_t *kbd);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_KEYBOARD_H */
