#include <doctest/doctest.h>
#include <cstring>

extern "C" {
#include <ms0515/core/keyboard.h>
}

TEST_SUITE("Keyboard") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static ms0515_keyboard_t make_kbd()
{
    ms0515_keyboard_t kbd;
    kbd_init(&kbd);
    return kbd;
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

static void tick_n(ms0515_keyboard_t *kbd, int n)
{
    for (int i = 0; i < n; i++)
        kbd_tick(kbd);
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

} /* TEST_SUITE */
