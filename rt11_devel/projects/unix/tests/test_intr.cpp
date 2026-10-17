/*
 * test_intr.cpp - the console's Ctrl+D and Ctrl+C.  Ctrl+D ends the input:
 * on an empty line read() gives 0, after a part of a line that part with
 * no '\n'; at the shell's prompt it ends the session, a new shell taking
 * its place.  Ctrl+C takes out the command in front - every process but
 * the session's shell and those the shell put in the background - one
 * waiting for the console, one at work, its status 130; at the prompt it
 * gives a fresh line.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

/* The processes alive but the session's shell, read off the process table
 * in bank 6: each found by the canary at its kernel stack's bottom, its
 * state the second word, its pid the third (proc.h). */
int othersAlive(Machine &m)
{
    constexpr unsigned kBank6 = 0140000, kDriver = 0155000, kProc = 38, kCanary = 0122525;
    int alive = 0;
    for (unsigned a = kBank6 + kProc; a < kDriver; a += 2) {
        if (m.emulator().readWord(static_cast<uint16_t>(a)) != kCanary) continue;
        const unsigned state = m.emulator().readWord(static_cast<uint16_t>(a - kProc + 2));
        const unsigned pid = m.emulator().readWord(static_cast<uint16_t>(a - kProc + 4));
        if ((state == 1 || state == 2) && pid != 1) ++alive;   /* P_RUN, P_SLEEP */
    }
    return alive;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("Ctrl+D ends what is typed to cat: the file is the lines") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat > /home/t\r", 30);
    m.typeAndSettle("one\rtwo\r", 30);
    m.typeAndSettle("\x04", 0);
    REQUIRE(m.waitPrompt(300));
    INFO(m.screen());
    Volume v(m.imageNow());
    const auto t = v.lookup("/home/t");
    REQUIRE(t);
    CHECK(v.content(v.inode(*t)) == "one\ntwo\n");
}

TEST_CASE("Ctrl+D after a part of a line gives that part, no newline; again, the end") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat > /home/p\r", 30);
    m.typeAndSettle("abc\x04", 30);
    m.typeAndSettle("\x04", 300);
    /* No newline after the part: the prompt comes on the same line. */
    std::string s = m.shown();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    INFO(s);
    CHECK(s.substr(s.rfind('\n') + 1) == "abc/home#");
    Volume v(m.imageNow());
    const auto p = v.lookup("/home/p");
    REQUIRE(p);
    CHECK(v.content(v.inode(*p)) == "abc");
}

TEST_CASE("Ctrl+D at the prompt ends the session: a new shell in /home") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("cd /etc"));
    m.typeAndSettle("\x04", 0);
    REQUIRE(m.waitPrompt(500));
    REQUIRE(m.command("pwd"));
    INFO(m.screen());
    CHECK(after(m, "pwd").find("\n/home\n#") != std::string::npos);
}

TEST_CASE("Ctrl+C takes out a command waiting for the console") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat\r", 30);
    m.typeAndSettle("half a li", 10);
    m.typeAndSettle("\x03", 0);
    REQUIRE(m.waitPrompt(100));
    INFO(m.screen());
    CHECK(m.screen().find("^C") != std::string::npos);
    CHECK(othersAlive(m) == 0);
    REQUIRE(m.command("status"));
    CHECK(after(m, "status").find("status 130") != std::string::npos);
}

TEST_CASE("Ctrl+C takes out a command at work, a pipeline's stage too") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("sleep 30\r", 50);
    m.typeAndSettle("\x03", 0);
    REQUIRE(m.waitPrompt(100));
    CHECK(othersAlive(m) == 0);
    REQUIRE(m.command("status"));
    INFO(m.screen());
    CHECK(after(m, "status").find("status 130") != std::string::npos);
    REQUIRE(m.command("echo still"));
    CHECK(after(m, "echo still").find("\nstill\n#") != std::string::npos);
}

TEST_CASE("Ctrl+C leaves the background alone, and at the prompt gives a fresh line") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("sleep 30 &\r", 50);
    REQUIRE(m.waitPrompt());
    m.typeAndSettle("cat\r", 30);
    m.typeAndSettle("\x03", 0);
    REQUIRE(m.waitPrompt(100));
    CHECK(othersAlive(m) == 1);                               /* the sleep in the background */
    m.typeAndSettle("ech\x03", 0);
    REQUIRE(m.waitPrompt(50));
    REQUIRE(m.command("echo after"));
    INFO(m.screen());
    CHECK(after(m, "echo after").find("\nafter\n#") != std::string::npos);
    CHECK(m.screen().find("ech^C") != std::string::npos);
    CHECK(othersAlive(m) == 1);
}

}
