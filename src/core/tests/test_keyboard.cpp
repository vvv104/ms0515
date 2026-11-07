#include <doctest/doctest.h>
#include <cstring>

extern "C" {
#include <ms0515/core/keyboard.h>
#include <ms0515/core/timer.h>
}

TEST_SUITE("Keyboard") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* The USART's receive clock is the timer's counter 0, as on the board;
 * the tests clock the USART from a timer of their own, programmed as the
 * ROM programs it (mode 3, 26 from 2 MHz) unless a test says otherwise. */
static ms0515_timer_t g_timer;

static void program_clock(uint8_t control, int divisor)
{
    timer_write(&g_timer, 3, control);
    if (divisor >= 0) {
        timer_write(&g_timer, 0, static_cast<uint8_t>(divisor & 0xFF));
        timer_write(&g_timer, 0, static_cast<uint8_t>(divisor >> 8));
    }
}

static ms0515_keyboard_t make_kbd()
{
    ms0515_keyboard_t kbd;
    kbd_init(&kbd);
    timer_init(&g_timer);
    program_clock(0x36, 26);
    return kbd;
}

/* `cycles` CPU cycles of the machine: the timer ticks at 2 MHz (4 ticks
 * in 15 cycles) and every rise of its OUT0 clocks the USART. */
static void advance(ms0515_keyboard_t *kbd, long cycles)
{
    static bool out_was = false;
    static long quarters = 0;               /* the fraction of a tick carried between calls */
    for (long c = 0; c < cycles; ++c) {
        quarters += 4;
        while (quarters >= 15) {
            quarters -= 15;
            timer_tick(&g_timer);
            const bool out = timer_get_out(&g_timer, 0);
            if (out && !out_was) kbd_clock_edge(kbd);
            out_was = out;
        }
        kbd_tick(kbd, 1);
    }
}

/* Cycles until RxRDY, ticking 16 at a time; -1 within `limit` if never. */
static long until_rxrdy(ms0515_keyboard_t *kbd, long limit)
{
    for (long t = 0; t < limit; t += 16) {
        if (kbd_read(kbd, 1) & KBD_STATUS_RXRDY) return t;
        advance(kbd, 16);
    }
    return -1;
}

/*
 * Perform the standard i8251 init sequence:
 *   1. Write mode byte (reg 1)
 *   2. Write command byte (reg 1)
 * The init_step state machine expects mode first, then command.
 */
static void init_usart(ms0515_keyboard_t *kbd)
{
    /* Mode: 8-bit, 1 stop, no parity, 16× baud — 0x4E */
    kbd_write(kbd, 1, 0x4E);
    /* Command: TX enable, RX enable, DTR — 0x37 */
    kbd_write(kbd, 1, 0x37);
}

/* n of the old 273 us ticks: 20 of them were a character's time and more. */
static void tick_n(ms0515_keyboard_t *kbd, int n)
{
    advance(kbd, 2048L * n);
}

/* ── Init ────────────────────────────────────────────────────────────────── */

TEST_CASE("kbd_init: TX ready set, RX ready clear") {
    auto kbd = make_kbd();

    uint8_t st = kbd_read(&kbd, 1);
    CHECK((st & KBD_STATUS_TXRDY) != 0);
    CHECK((st & KBD_STATUS_RXRDY) == 0);
}

TEST_CASE("kbd_init: no IRQ pending") {
    auto kbd = make_kbd();
    CHECK(kbd.irq == false);
}

/* ── i8251 init sequence ─────────────────────────────────────────────────── */

TEST_CASE("init sequence advances init_step") {
    auto kbd = make_kbd();
    CHECK(kbd.init_step == 0);

    kbd_write(&kbd, 1, 0x4E);  /* mode (baud factor non-zero → step 1) */
    CHECK(kbd.init_step == 1);

    /* After mode, next write to reg 1 is command — step stays at 1
     * (command does not advance init_step further unless internal reset) */
    kbd_write(&kbd, 1, 0x37);  /* command */
    CHECK(kbd.init_step == 1);
}

/* ── FIFO and data delivery ──────────────────────────────────────────────── */

TEST_CASE("scancode pushed to FIFO is delivered after ticks") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    kbd_push_scancode(&kbd, 0x42);
    CHECK(kbd.fifo_count == 1);

    /* Tick enough for byte to transfer from FIFO to rx_data */
    tick_n(&kbd, 20);

    uint8_t st = kbd_read(&kbd, 1);
    CHECK((st & KBD_STATUS_RXRDY) != 0);

    uint8_t data = kbd_read(&kbd, 0);
    CHECK(data == 0x42);
}

TEST_CASE("reading data register clears RXRDY") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    kbd_push_scancode(&kbd, 0x10);
    tick_n(&kbd, 20);

    /* Read data — should clear RXRDY */
    kbd_read(&kbd, 0);

    uint8_t st = kbd_read(&kbd, 1);
    CHECK((st & KBD_STATUS_RXRDY) == 0);
}

TEST_CASE("multiple scancodes delivered in FIFO order") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    kbd_push_scancode(&kbd, 0xAA);
    kbd_push_scancode(&kbd, 0xBB);
    kbd_push_scancode(&kbd, 0xCC);

    /* First byte */
    tick_n(&kbd, 20);
    CHECK(kbd_read(&kbd, 0) == 0xAA);

    /* Second byte */
    tick_n(&kbd, 20);
    CHECK(kbd_read(&kbd, 0) == 0xBB);

    /* Third byte */
    tick_n(&kbd, 20);
    CHECK(kbd_read(&kbd, 0) == 0xCC);
}

/* ── FIFO overflow ───────────────────────────────────────────────────────── */

TEST_CASE("FIFO drops bytes when full (16 entries)") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    for (int i = 0; i < 20; i++)
        kbd_push_scancode(&kbd, (uint8_t)i);

    /* FIFO size is 16 — should not exceed that */
    CHECK(kbd.fifo_count <= 16);
}

/* ── FIFO flush ──────────────────────────────────────────────────────────── */

TEST_CASE("kbd_flush_fifo discards all pending bytes") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    kbd_push_scancode(&kbd, 0x01);
    kbd_push_scancode(&kbd, 0x02);
    CHECK(kbd.fifo_count == 2);

    kbd_flush_fifo(&kbd);
    CHECK(kbd.fifo_count == 0);
}

/* ── IRQ ─────────────────────────────────────────────────────────────────── */

TEST_CASE("IRQ asserted when RX data is ready") {
    auto kbd = make_kbd();
    init_usart(&kbd);

    kbd_push_scancode(&kbd, 0x55);
    tick_n(&kbd, 20);

    CHECK(kbd.irq == true);

    /* Read data — IRQ should deassert after next tick */
    kbd_read(&kbd, 0);
    tick_n(&kbd, 1);
    CHECK(kbd.irq == false);
}

/* ── Reset ───────────────────────────────────────────────────────────────── */

TEST_CASE("kbd_reset clears state but keeps TX ready") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    kbd_push_scancode(&kbd, 0x01);
    tick_n(&kbd, 20);

    kbd_reset(&kbd);

    CHECK(kbd.fifo_count == 0);
    CHECK(kbd.rx_ready == false);
    CHECK((kbd_read(&kbd, 1) & KBD_STATUS_TXRDY) != 0);
}

/* ── TXRDY status-bit semantics ──────────────────────────────────────────── */

/*
 * Per the Intel 8251 datasheet, the TxRDY *pin* is gated by TxEN
 * (and CTS/DSR), but the STATUS REGISTER bit that the CPU reads is
 * independent of TxEN — it reflects only the transmitter buffer
 * state.  Rodionov's keyboard driver clears the command register
 * (TxEN=0) before its host→keyboard transmit routine and then polls
 * the status TXRDY bit waiting for buffer-empty; if we gate the bit
 * on TxEN, the loop never exits and the shell hangs the first time
 * the user types anything after the date prompt.
 */
TEST_CASE("TXRDY status bit is independent of TxEN") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    CHECK((kbd_read(&kbd, 1) & KBD_STATUS_TXRDY) != 0);

    /* Clear command register — both TxEN and RxEN drop to 0. */
    kbd_write(&kbd, 1, 0x00);
    CHECK(kbd.command == 0x00);

    /* TXRDY status bit must still be set: the transmitter buffer is
     * empty regardless of the enable bits. */
    CHECK((kbd_read(&kbd, 1) & KBD_STATUS_TXRDY) != 0);
}

/* ── The receive clock: the timer's counter 0 ─────────────────────────────── */

TEST_CASE("clock: with the ROM's counter 0 a scancode arrives in a character's time") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    kbd_push_scancode(&kbd, 0x42);
    const long t = until_rxrdy(&kbd, 4 * KBD_BYTE_CYCLES);
    CHECK(t >= KBD_BYTE_CYCLES - 64);
    CHECK(t <= KBD_BYTE_CYCLES + 64);
    CHECK(kbd_read(&kbd, 0) == 0x42);
    CHECK((kbd_read(&kbd, 1) & KBD_STATUS_FE) == 0);
}

TEST_CASE("clock: counter 0 stopped, the scancode never arrives and no interrupt is raised") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    program_clock(0x30, -1);                /* mode 0, no count: OUT0 stands */
    kbd_push_scancode(&kbd, 0x42);
    CHECK(until_rxrdy(&kbd, 10 * KBD_BYTE_CYCLES) == -1);
    CHECK(!kbd.irq);
    CHECK(kbd.fifo_count == 0);             /* the character went by, lost */
}

TEST_CASE("clock: counter 0 reloaded with 1 - EX.SYS's page number on a board without the wire - the same death") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    program_clock(0x36, 1);
    kbd_push_scancode(&kbd, 0x42);
    CHECK(until_rxrdy(&kbd, 10 * KBD_BYTE_CYCLES) == -1);
    CHECK((kbd_read(&kbd, 1) & KBD_STATUS_FE) != 0);
}

TEST_CASE("clock: the ROM's words again, and the next scancode arrives") {
    auto kbd = make_kbd();
    init_usart(&kbd);
    program_clock(0x30, -1);
    kbd_push_scancode(&kbd, 0x11);
    CHECK(until_rxrdy(&kbd, 4 * KBD_BYTE_CYCLES) == -1);
    program_clock(0x36, 26);
    kbd_push_scancode(&kbd, 0x22);
    CHECK(until_rxrdy(&kbd, 4 * KBD_BYTE_CYCLES) >= 0);
    CHECK(kbd_read(&kbd, 0) == 0x22);
}

} /* TEST_SUITE */
