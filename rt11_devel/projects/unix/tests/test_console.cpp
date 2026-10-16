/*
 * test_console.cpp - the console through the kernel's system calls (stage
 * 2): init and the shell after it read and write it; the keyboard comes by
 * its interrupt, a line at a time with echo and erasing, and the clock by
 * the timer's.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

/* The machine booted to the process's login: prompt. */
void booted(Machine &m)
{
    REQUIRE(m.runUntil("login:", 1500));
    m.run(20);
}

/* The number after the last `word` on the screen, -1 when there is none. */
long lastNumberAfter(const std::string &screen, const std::string &word)
{
    const auto at = screen.rfind(word);
    if (at == std::string::npos) return -1;
    std::size_t i = at + word.size();
    while (i < screen.size() && screen[i] == ' ') ++i;
    long n = -1;
    while (i < screen.size() && screen[i] >= '0' && screen[i] <= '9')
        n = (n < 0 ? 0 : n * 10) + (screen[i++] - '0');
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("a name typed after login: is echoed, read by the process as a line, and greeted") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    m.typeAndSettle("root\r", 50);
    m.waitPrompt();
    INFO(m.screen());
    CHECK(m.screen().find("login: root") != std::string::npos);    /* the echo */
    CHECK(m.screen().find("Hello, root") != std::string::npos);    /* the process's answer */
    CHECK(m.screen().find("Hello, root\n$") != std::string::npos);  /* and its next prompt */
}

TEST_CASE("an empty name is no name: login: is asked again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    m.typeAndSettle("\r", 50);
    m.typeAndSettle("   \r", 50);
    INFO(m.screen());
    CHECK(m.screen().find("Hello") == std::string::npos);
    m.typeAndSettle("root\r", 50);
    m.waitPrompt();
    CHECK(m.screen().find("Hello, root") != std::string::npos);
    std::size_t logins = 0;
    for (auto at = m.screen().find("login:"); at != std::string::npos; at = m.screen().find("login:", at + 1)) ++logins;
    CHECK(logins == 3);
}

TEST_CASE("letters bare are small, Latin and Russian alike; with Shift capital") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    /* The keys of R, O, O, T pressed bare - the Typist's Key presses carry
     * no Shift - and then a name typed with a capital. */
    for (auto k : {ms0515::Key::R, ms0515::Key::O, ms0515::Key::O, ms0515::Key::T}) m.tapBare(k);
    m.typeAndSettle("\r", 50);
    m.waitPrompt();
    m.typeAndSettle("Ab\r", 50);
    INFO(m.screen());
    CHECK(m.screen().find("Hello, root") != std::string::npos);
    CHECK(m.screen().find("Ab: not found") != std::string::npos);
}

TEST_CASE("a Russian name, small letters bare, is read as it was typed") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    const std::string vanya = "\xD7\xC1\xCE\xD1";                  /* KOI-8: в а н я */
    m.typeAndSettle(vanya + "\r", 60);
    INFO(m.screen());
    CHECK(m.screen().find("Hello, " + vanya) != std::string::npos);
}

TEST_CASE("a line is edited before it is read: backspace takes the last character back") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    m.typeAndSettle("roox\b\bot\r", 50);
    INFO(m.screen());
    CHECK(m.screen().find("Hello, root") != std::string::npos);
    CHECK(m.screen().find("Hello, roo\n") == std::string::npos);
}

TEST_CASE("the clock goes: the time asked twice three seconds apart differs by about three") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    m.typeAndSettle("root\r", 30);
    m.waitPrompt();
    m.typeAndSettle("t\r", 30);
    const long first = lastNumberAfter(m.screen(), "time ");
    m.run(150);                                                    /* 3 s at 50 frames a second */
    m.typeAndSettle("t\r", 30);
    const long second = lastNumberAfter(m.screen(), "time ");
    INFO(m.screen());
    CAPTURE(first);
    CAPTURE(second);
    REQUIRE(first >= 0);
    CHECK(second - first >= 3);
    CHECK(second - first <= 5);
}

TEST_CASE("an unknown command is reported, and the shell asks again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    booted(m);
    m.typeAndSettle("root\r", 30);
    m.waitPrompt();
    REQUIRE(m.command("xyz"));
    INFO(m.screen());
    CHECK(m.screen().find("xyz: not found\n$") != std::string::npos);
}

}
