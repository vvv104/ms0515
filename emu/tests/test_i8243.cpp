#include <doctest/doctest.h>

extern "C" {
#include <ms0515/i8243.h>
}

TEST_SUITE("i8243") {

/* Encoding helper — produces the nibble the CPU would drive on P2[3:0]
 * for a given (cmd, port) pair: cmd in bits 3..2, port in bits 1..0. */
static uint8_t cmd_nibble(uint8_t cmd, int port)
{
    return (uint8_t)(((cmd & 3) << 2) | (port & 3));
}

/* Run one full transaction: PROG falls with the command nibble, the
 * data nibble is then driven, PROG rises.  For READ commands the data
 * nibble is irrelevant. */
static void transact(i8243_t &e, uint8_t cmd, int port, uint8_t data)
{
    i8243_p2_write(&e, cmd_nibble(cmd, port));
    i8243_prog(&e, false);
    i8243_p2_write(&e, data & 0x0F);
    i8243_prog(&e, true);
}

/* ── Reset state ─────────────────────────────────────────────────────────── */

TEST_CASE("Reset leaves every output latch and every input drive at 0xF") {
    i8243_t e; i8243_init(&e);
    for (int p = 0; p < 4; p++) {
        CHECK(i8243_get_port(&e, p) == 0x0F);
    }
    /* PROG is idle high after reset. */
    CHECK(i8243_p2_read(&e) == 0x0F);
}

/* ── WRITE ───────────────────────────────────────────────────────────────── */

TEST_CASE("WRITE replaces the addressed port latch on PROG rising edge") {
    i8243_t e; i8243_init(&e);
    transact(e, /*WRITE=*/1, /*port=*/2, /*data=*/0xA);
    CHECK(i8243_get_port(&e, 2) == 0xA);
    /* Other ports unaffected */
    CHECK(i8243_get_port(&e, 0) == 0x0F);
    CHECK(i8243_get_port(&e, 1) == 0x0F);
    CHECK(i8243_get_port(&e, 3) == 0x0F);
}

TEST_CASE("WRITE addresses each of the four ports") {
    i8243_t e; i8243_init(&e);
    transact(e, 1, 0, 0x1);
    transact(e, 1, 1, 0x2);
    transact(e, 1, 2, 0x4);
    transact(e, 1, 3, 0x8);
    CHECK(i8243_get_port(&e, 0) == 0x1);
    CHECK(i8243_get_port(&e, 1) == 0x2);
    CHECK(i8243_get_port(&e, 2) == 0x4);
    CHECK(i8243_get_port(&e, 3) == 0x8);
}

/* ── ORLD ────────────────────────────────────────────────────────────────── */

TEST_CASE("ORLD merges bits into the existing latch") {
    i8243_t e; i8243_init(&e);
    transact(e, 1, 1, 0x3);                /* WRITE 0x3 → latch = 0x3 */
    transact(e, 2, 1, 0xC);                /* ORLD  0xC → 0x3 | 0xC = 0xF */
    CHECK(i8243_get_port(&e, 1) == 0xF);
}

/* ── ANLD ────────────────────────────────────────────────────────────────── */

TEST_CASE("ANLD masks bits out of the existing latch") {
    i8243_t e; i8243_init(&e);
    /* Latch starts at 0xF after reset; ANLD with 0x6 → 0x6. */
    transact(e, 3, 3, 0x6);
    CHECK(i8243_get_port(&e, 3) == 0x6);
}

/* ── READ ────────────────────────────────────────────────────────────────── */

TEST_CASE("READ drives the addressed port's latch onto P2[3:0]") {
    i8243_t e; i8243_init(&e);
    transact(e, 1, 0, 0x9);                /* set port 0 to 0x9 */

    /* Begin a READ on port 0: command nibble + PROG falls.            */
    i8243_p2_write(&e, cmd_nibble(0, 0));
    i8243_prog(&e, false);
    CHECK(i8243_p2_read(&e) == 0x9);
    /* When PROG rises again the expander stops driving. */
    i8243_prog(&e, true);
    CHECK(i8243_p2_read(&e) == 0x0F);
}

TEST_CASE("READ value reflects the host-injected input AND with the latch") {
    /* Quasi-bidirectional model: the host can pull lines low against
     * a high latch but cannot raise lines that the latch holds low. */
    i8243_t e; i8243_init(&e);
    /* Latch all-high (default), inject 0x5 on port 2. */
    i8243_set_input(&e, 2, 0x5);

    i8243_p2_write(&e, cmd_nibble(0, 2));
    i8243_prog(&e, false);
    CHECK(i8243_p2_read(&e) == 0x5);       /* F & 5 = 5 */
    i8243_prog(&e, true);

    /* Drop the latch to 0x6 — the host injection 0x5 is then masked
     * back to 0x4 by the latch (a line driven low cannot be read high). */
    transact(e, 1, 2, 0x6);
    i8243_p2_write(&e, cmd_nibble(0, 2));
    i8243_prog(&e, false);
    CHECK(i8243_p2_read(&e) == 0x4);       /* 6 & 5 = 4 */
    i8243_prog(&e, true);
}

/* ── PROG semantics ──────────────────────────────────────────────────────── */

TEST_CASE("Idle PROG-high reads as not-driving regardless of past command") {
    i8243_t e; i8243_init(&e);
    transact(e, 1, 0, 0x5);                 /* leaves PROG high          */
    CHECK(i8243_p2_read(&e) == 0x0F);
}

TEST_CASE("Repeating PROG transitions in the same direction is idempotent") {
    /* Two falling edges in a row should not re-latch the command — only
     * a high→low transition counts as an edge. */
    i8243_t e; i8243_init(&e);
    i8243_p2_write(&e, cmd_nibble(1, 1));   /* WRITE port 1 setup */
    i8243_prog(&e, false);
    i8243_p2_write(&e, 0x07);                /* data — write-window  */
    /* Spurious second "false" call — should be a no-op since prog
     * is already low. */
    i8243_prog(&e, false);
    i8243_prog(&e, true);
    CHECK(i8243_get_port(&e, 1) == 0x07);
}

} /* TEST_SUITE */
