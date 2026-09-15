/*
 * test_mech_events.cpp — the machine's mechanical events, for a host that
 * plays their sounds: the drive's spindle motor starting and stopping,
 * a seek and each pulse of the head positioner; the MS7004's keyclick
 * and bell.  Nothing here changes what the machine does - the events are
 * a report of what it already does.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/floppy.h>
#include <ms0515/core/keyboard.h>
#include <ms0515/core/ms7004.h>
}

#include <utility>
#include <vector>

namespace {

struct Log {
    std::vector<std::pair<int, int>> events;   /* (event, arg) */
    static void take(void *ud, int event, int arg)
    {
        static_cast<Log *>(ud)->events.emplace_back(event, arg);
    }
    static void take1(void *ud, int event)
    {
        static_cast<Log *>(ud)->events.emplace_back(event, 0);
    }
};

/* Run the FDC until it is idle again (a seek of N tracks takes N step
 * periods plus the settle), in slices small enough to see every pulse. */
void runToIdle(ms0515_floppy_t &fdc)
{
    for (int i = 0; i < 200000 && fdc.state != FDC_STATE_IDLE; ++i)
        fdc_tick(&fdc, 100);
}

} // namespace

TEST_SUITE("mechanical events") {

TEST_CASE("the spindle: a drive's motor reports once when it starts and once when it stops") {
    ms0515_floppy_t fdc;
    fdc_init(&fdc);
    Log log;
    fdc_set_mech_callback(&fdc, &Log::take, &log);

    fdc_select(&fdc, 0, 0, true);                 /* drive 0 side 0, motor on */
    fdc_select(&fdc, 0, 0, true);                 /* again: nothing new */
    fdc_select(&fdc, 0, 1, true);                 /* the other side of the same drive: same motor */
    REQUIRE(log.events.size() == 1);
    CHECK(log.events[0] == std::make_pair(int(FDC_MECH_MOTOR_ON), 0));

    /* One motor bit, one selected drive: selecting the other with the
     * motor still on moves the spindle over. */
    fdc_select(&fdc, 1, 0, true);
    REQUIRE(log.events.size() == 3);
    CHECK(log.events[1] == std::make_pair(int(FDC_MECH_MOTOR_OFF), 0));
    CHECK(log.events[2] == std::make_pair(int(FDC_MECH_MOTOR_ON), 1));

    /* And the bit cleared stops what turns, whichever drive is selected
     * then - which is what the guest does after a read: it clears the
     * motor while selecting another unit. */
    fdc_select(&fdc, 0, 1, false);
    REQUIRE(log.events.size() == 4);
    CHECK(log.events[3] == std::make_pair(int(FDC_MECH_MOTOR_OFF), 1));

    fdc_select(&fdc, 1, 0, true);
    log.events.clear();
    fdc_reset(&fdc);                              /* reset stops what still spins */
    REQUIRE(log.events.size() == 1);
    CHECK(log.events[0] == std::make_pair(int(FDC_MECH_MOTOR_OFF), 1));
}

TEST_CASE("a seek reports its length and direction once, and nothing per pulse") {
    ms0515_floppy_t fdc;
    fdc_init(&fdc);
    Log log;
    fdc_set_mech_callback(&fdc, &Log::take, &log);
    fdc_select(&fdc, 0, 0, true);
    log.events.clear();

    fdc_write(&fdc, 3, 5);                        /* data register: the target track */
    fdc_write(&fdc, 0, 0x10);                     /* SEEK, fastest rate */
    REQUIRE(log.events.size() == 1);              /* announced at the command, before the head moves */
    CHECK(log.events[0] == std::make_pair(int(FDC_MECH_SEEK), 5));
    runToIdle(fdc);
    CHECK(log.events.size() == 1);                /* and the five pulses say nothing of their own */

    log.events.clear();
    fdc_write(&fdc, 0, 0x00);                     /* RESTORE: back to track 0 */
    runToIdle(fdc);
    REQUIRE(log.events.size() == 1);
    CHECK(log.events[0] == std::make_pair(int(FDC_MECH_SEEK), -5));

    log.events.clear();
    fdc_write(&fdc, 0, 0x00);                     /* already there: nothing to report */
    runToIdle(fdc);
    CHECK(log.events.empty());

    fdc_write(&fdc, 0, 0x40);                     /* STEP IN: one track toward the hub */
    runToIdle(fdc);
    REQUIRE(log.events.size() == 1);
    CHECK(log.events[0] == std::make_pair(int(FDC_MECH_SEEK), 1));
}

TEST_CASE("the keyboard clicks on every auto-repeat while the host allows it, rings on command and at power-on") {
    ms0515_keyboard_t uart;
    kbd_init(&uart);
    ms7004_t kbd;
    ms7004_init(&kbd, &uart);
    Log log;
    ms7004_set_sound_callback(&kbd, &Log::take1, &log);
    kbd.repeat_enabled = true;

    ms7004_key(&kbd, MS7004_KEY_A, true);         /* a make is silent - the firmware only sends the code */
    ms7004_key(&kbd, MS7004_KEY_A, false);
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, true);
    ms7004_key(&kbd, MS7004_KEY_SHIFT_L, false);
    CHECK(log.events.empty());

    /* Held: every repeat the keyboard sends clicks - one per scan, the way
     * a host drives the clock, never a burst to catch up with. */
    ms7004_key(&kbd, MS7004_KEY_A, true);
    for (uint32_t t = 0; t <= 1000; t += 20) ms7004_tick(&kbd, t);
    const auto clicks = log.events.size();
    CHECK(clicks >= 2);
    for (const auto &e : log.events) CHECK(e.first == MS7004_SOUND_CLICK);
    ms7004_key(&kbd, MS7004_KEY_A, false);

    ms7004_host_byte(&kbd, 0x99);                 /* keyclick disabled: a game's startup */
    ms7004_key(&kbd, MS7004_KEY_A, true);
    for (uint32_t t = 1000; t <= 2000; t += 20) ms7004_tick(&kbd, t);
    ms7004_key(&kbd, MS7004_KEY_A, false);
    CHECK(log.events.size() == clicks);

    ms7004_host_byte(&kbd, 0x1B);                 /* keyclick enabled + a byte the firmware ignores */
    ms7004_host_byte(&kbd, 0x82);
    ms7004_key(&kbd, MS7004_KEY_A, true);
    for (uint32_t t = 2000; t <= 3000; t += 20) ms7004_tick(&kbd, t);
    ms7004_key(&kbd, MS7004_KEY_A, false);
    CHECK(log.events.size() > clicks);

    log.events.clear();
    ms7004_host_byte(&kbd, 0x9F);                 /* "produce click" */
    ms7004_host_byte(&kbd, 0xA7);                 /* "produce bell" */
    REQUIRE(log.events.size() == 2);
    CHECK(log.events[0].first == MS7004_SOUND_CLICK);
    CHECK(log.events[1].first == MS7004_SOUND_BELL);

    ms7004_host_byte(&kbd, 0xA1);                 /* bell disabled */
    ms7004_host_byte(&kbd, 0xA7);
    CHECK(log.events.size() == 2);

    log.events.clear();
    ms7004_reset(&kbd);                           /* the computer resetting is not the keyboard's power-on */
    CHECK(log.events.empty());
}

TEST_CASE("a host that stalls gets one repeat, not a burst to make up for it") {
    ms0515_keyboard_t uart;
    kbd_init(&uart);
    ms7004_t kbd;
    ms7004_init(&kbd, &uart);
    Log log;
    ms7004_set_sound_callback(&kbd, &Log::take1, &log);

    ms7004_key(&kbd, MS7004_KEY_A, true);
    ms7004_tick(&kbd, 250);                       /* the typematic delay is up */
    CHECK(log.events.size() == 1);
    ms7004_tick(&kbd, 5000);                      /* the host was away for five seconds */
    CHECK(log.events.size() == 2);                /* one repeat, and the clock starts afresh */
    ms7004_tick(&kbd, 5010);
    CHECK(log.events.size() == 2);
    ms7004_tick(&kbd, 5040);
    CHECK(log.events.size() == 3);
}

} // TEST_SUITE
