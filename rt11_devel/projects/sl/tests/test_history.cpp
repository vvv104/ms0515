/*
 * test_history.cpp - the history is a ring.
 *
 * DEC's two buffers hold as many lines as fit: Up walks to the older ones,
 * Down back to the newer; under the newest is an empty line; on a line the
 * history was not walked from, Down is still DEC's saved line.
 */
#include "SlMachine.hpp"

using K = ms0515::Key;

TEST_SUITE("SL history ring") {

TEST_CASE("Up walks back through more than two lines, Down forward") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99\r");
    m.type("DATE 2-JAN-99\r");
    m.type("DATE 3-JAN-99\r");
    m.type("DATE 4-JAN-99\r");
    m.key(K::Up);
    CHECK(m.shown() == ".DATE 4-JAN-99");
    m.key(K::Up);
    m.key(K::Up);
    m.key(K::Up);
    CHECK(m.shown() == ".DATE 1-JAN-99");
    m.key(K::Down);
    CHECK(m.shown() == ".DATE 2-JAN-99");
    m.type("\r");                               /* a line of the ring runs */
    m.type("DATE\r");
    INFO("screen:\n" << m.screen());
    CHECK(m.rowsEqual("2-Jan-99") == 1);
}

TEST_CASE("past the oldest Up only beeps; under the newest is an empty line") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99\r");
    m.key(K::Up);                               /* DATE 1-JAN-99 */
    m.key(K::Up);                               /* SET SL ON has no line here: the oldest */
    m.key(K::Up);
    CHECK(m.shown() == ".DATE 1-JAN-99");
    m.key(K::Down);
    CHECK(m.shown() == ".");
}

TEST_CASE("a line run twice is kept once, an empty one not at all") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99\r");
    m.type("DATE\r");
    m.type("DATE\r");
    m.type("\r");
    m.key(K::Up);
    CHECK(m.shown() == ".DATE");
    m.key(K::Up);
    CHECK(m.shown() == ".DATE 1-JAN-99");
}

TEST_CASE("the oldest lines go when the ring is full") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    /* Fifteen bytes a line with its zero; 160 hold ten. */
    for (int day = 10; day < 25; ++day)
        m.type("DATE " + std::to_string(day) + "-JAN-99\r");
    for (int i = 0; i < 10; ++i) m.key(K::Up);
    CHECK(m.shown() == ".DATE 15-JAN-99");      /* the tenth back from 24 */
    m.key(K::Up);
    CHECK(m.shown() == ".DATE 15-JAN-99");      /* and no eleventh */
}

TEST_CASE("Down on a fresh line is DEC's: the line saved with GOLD Down") {
    if (!sl::built()) { MESSAGE("no SL.SYS - skipped"); return; }
    sl::SlMachine m;
    m.type("DATE 1-JAN-99");
    m.gold(K::Down);
    m.ctrl('U');
    m.type("DATE\r");
    m.key(K::Down);
    CHECK(m.shown() == ".DATE 1-JAN-99");
}

}
