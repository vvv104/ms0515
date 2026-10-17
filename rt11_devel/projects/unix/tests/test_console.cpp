/*
 * test_console.cpp - the console through the kernel's system calls (stage
 * 2): the shell reads and writes it; the keyboard comes by its interrupt, a
 * line at a time with echo and erasing, and the clock by the timer's.
 */
#include "Machine.hpp"

#include <string>

using unixos::Machine;

namespace {

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

std::size_t count(const std::string &s, const std::string &what)
{
    std::size_t n = 0;
    for (auto at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) ++n;
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("a line typed is echoed and read by the shell as a line") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
    REQUIRE(m.command("echo hi there"));
    INFO(m.screen());
    CHECK(m.screen().find("# echo hi there\n") != std::string::npos);   /* the echo */
    CHECK(m.screen().find("\nhi there\n#") != std::string::npos);       /* the program's, and the prompt */
}

TEST_CASE("an empty line is no command: the shell asks again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
    const std::size_t before = count(m.screen(), "#");
    REQUIRE(m.command(""));
    REQUIRE(m.command("   "));
    INFO(m.screen());
    CHECK(count(m.screen(), "#") == before + 2);
    CHECK(m.screen().find("not found") == std::string::npos);
}

TEST_CASE("letters bare are small, Latin and Russian alike; with Shift capital") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
    /* The keys of R, O, O, T pressed bare - the Typist's Key presses carry
     * no Shift - and then a name typed with a capital. */
    for (auto k : {ms0515::Key::R, ms0515::Key::O, ms0515::Key::O, ms0515::Key::T}) m.tapBare(k);
    m.typeAndSettle("\r", 50);
    m.waitPrompt();
    m.typeAndSettle("Ab\r", 50);
    INFO(m.screen());
    CHECK(m.screen().find("root: not found") != std::string::npos);
    CHECK(m.screen().find("Ab: not found") != std::string::npos);
}

TEST_CASE("a Russian word, small letters bare, is read as it was typed") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
    const std::string vanya = "\xD7\xC1\xCE\xD1";                  /* KOI-8: в а н я */
    REQUIRE(m.command("echo " + vanya));
    INFO(m.screen());
    CHECK(m.screen().find("\n" + vanya + "\n#") != std::string::npos);
}

TEST_CASE("a line is edited before it is read: backspace takes the last character back") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
    m.typeAndSettle("echo roox\b\bot\r", 50);
    m.waitPrompt();
    INFO(m.screen());
    CHECK(m.screen().find("\nroot\n#") != std::string::npos);
    CHECK(m.screen().find("\nroo\n") == std::string::npos);
}

TEST_CASE("the clock goes: the time asked twice three seconds apart differs by about three") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    REQUIRE(m.boot());
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
    REQUIRE(m.boot());
    REQUIRE(m.command("xyz"));
    INFO(m.screen());
    CHECK(m.screen().find("xyz: not found\n#") != std::string::npos);
}

}
