#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/timer.h>
}

TEST_SUITE("Timer") {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static ms0515_timer_t make_timer()
{
    ms0515_timer_t t;
    timer_init(&t);
    return t;
}

/*
 * Write a control word to the 8253 control register (reg 3).
 *
 * Control word format:
 *   bits 7-6: channel select (0-2)
 *   bits 5-4: R/W mode (0=latch, 1=LSB, 2=MSB, 3=LSB then MSB)
 *   bits 3-1: mode (0-5)
 *   bit 0:    BCD (0=binary, 1=BCD)
 */
static void program_channel(ms0515_timer_t *t, int ch, int rw, int mode)
{
    uint8_t ctrl = (uint8_t)((ch << 6) | (rw << 4) | (mode << 1));
    timer_write(t, 3, ctrl);
}

static void load_count_word(ms0515_timer_t *t, int ch, uint16_t count)
{
    timer_write(t, ch, (uint8_t)(count & 0xFF));
    timer_write(t, ch, (uint8_t)(count >> 8));
}

/* ── Init / Reset ────────────────────────────────────────────────────────── */

TEST_CASE("timer_init sets all outputs high") {
    auto t = make_timer();
    CHECK(timer_get_out(&t, 0) == true);
    CHECK(timer_get_out(&t, 1) == true);
    CHECK(timer_get_out(&t, 2) == true);
}

TEST_CASE("timer_reset restores init state") {
    auto t = make_timer();
    /* Program channel 0 and tick a bit */
    program_channel(&t, 0, 3, 0);
    load_count_word(&t, 0, 10);
    for (int i = 0; i < 5; i++) timer_tick(&t);

    timer_reset(&t);
    CHECK(timer_get_out(&t, 0) == true);
}

/* ── Mode 0: Interrupt on Terminal Count ─────────────────────────────────── */

TEST_CASE("mode 0: OUT goes low on load, high after N ticks") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 0);  /* Ch 0, LSB+MSB, mode 0 */
    load_count_word(&t, 0, 5);

    /* After loading, OUT should go low */
    CHECK(timer_get_out(&t, 0) == false);

    /* Tick 5 times — should reach terminal count */
    for (int i = 0; i < 5; i++)
        timer_tick(&t);

    CHECK(timer_get_out(&t, 0) == true);
}

/* ── Mode 2: Rate Generator ──────────────────────────────────────────────── */

TEST_CASE("mode 2: OUT pulses low every N ticks") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 2);  /* mode 2 */
    load_count_word(&t, 0, 4);

    /* OUT should be high initially after load */
    timer_tick(&t);  /* start counting */
    CHECK(timer_get_out(&t, 0) == true);

    /* After N ticks, OUT should pulse low then reload */
    int low_count = 0;
    for (int i = 0; i < 20; i++) {
        timer_tick(&t);
        if (!timer_get_out(&t, 0))
            low_count++;
    }
    /* Should have pulsed low multiple times (auto-reload) */
    CHECK(low_count > 1);
}

/* ── Mode 3: Square Wave Generator ───────────────────────────────────────── */

TEST_CASE("mode 3: OUT toggles to produce square wave") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 3);  /* mode 3 */
    load_count_word(&t, 0, 10);

    /* Run enough ticks to see transitions */
    int transitions = 0;
    bool prev = timer_get_out(&t, 0);
    for (int i = 0; i < 40; i++) {
        timer_tick(&t);
        bool cur = timer_get_out(&t, 0);
        if (cur != prev) transitions++;
        prev = cur;
    }

    /* A square wave with count=10 should toggle every 5 ticks.
     * In 40 ticks we expect ~8 transitions. */
    CHECK(transitions >= 4);
}

/* ── Gate control ────────────────────────────────────────────────────────── */

TEST_CASE("gate low inhibits counting in mode 0") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 0);
    load_count_word(&t, 0, 3);
    timer_set_gate(&t, 0, false);

    /* Tick with gate low — should not reach terminal count */
    for (int i = 0; i < 10; i++)
        timer_tick(&t);
    CHECK(timer_get_out(&t, 0) == false);

    /* Enable gate — should now count down */
    timer_set_gate(&t, 0, true);
    for (int i = 0; i < 5; i++)
        timer_tick(&t);
    CHECK(timer_get_out(&t, 0) == true);
}

/* ── Counter latch ───────────────────────────────────────────────────────── */

TEST_CASE("latch command freezes read value") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 2);   /* mode 2, LSB+MSB */
    load_count_word(&t, 0, 1000);

    /* Tick a few times to decrement */
    for (int i = 0; i < 10; i++)
        timer_tick(&t);

    /* Issue latch command (rw=0 in control word) */
    timer_write(&t, 3, 0x00);  /* ch=0, rw=0 (latch) */

    /* Read latched value — should be < 1000 */
    uint8_t lo = timer_read(&t, 0);
    uint8_t hi = timer_read(&t, 0);
    uint16_t latched = lo | ((uint16_t)hi << 8);
    CHECK(latched < 1000);
    CHECK(latched > 0);
}

/* ── LSB-only mode ───────────────────────────────────────────────────────── */

TEST_CASE("rw_mode=1 (LSB only) loads and reads low byte") {
    auto t = make_timer();

    program_channel(&t, 0, 1, 0);  /* rw=1 (LSB), mode 0 */
    timer_write(&t, 0, 5);         /* load count = 5 */

    /* Should count down from 5 */
    for (int i = 0; i < 5; i++)
        timer_tick(&t);
    CHECK(timer_get_out(&t, 0) == true);
}

/* ── MSB-only mode ───────────────────────────────────────────────────────── */

TEST_CASE("rw_mode=2 (MSB only) loads high byte") {
    auto t = make_timer();

    program_channel(&t, 0, 2, 0);  /* rw=2 (MSB), mode 0 */
    timer_write(&t, 0, 1);         /* load count = 0x0100 = 256 */

    /* Should count down from 256 */
    for (int i = 0; i < 256; i++)
        timer_tick(&t);
    CHECK(timer_get_out(&t, 0) == true);
}

/* ── Multiple channels ───────────────────────────────────────────────────── */

TEST_CASE("channels are independent") {
    auto t = make_timer();

    program_channel(&t, 0, 3, 0);
    load_count_word(&t, 0, 3);

    program_channel(&t, 1, 3, 0);
    load_count_word(&t, 1, 6);

    /* After 3 ticks: ch0 done, ch1 still counting */
    for (int i = 0; i < 3; i++)
        timer_tick(&t);

    CHECK(timer_get_out(&t, 0) == true);
    CHECK(timer_get_out(&t, 1) == false);

    /* After 3 more: ch1 done too */
    for (int i = 0; i < 3; i++)
        timer_tick(&t);
    CHECK(timer_get_out(&t, 1) == true);
}

} /* TEST_SUITE */
