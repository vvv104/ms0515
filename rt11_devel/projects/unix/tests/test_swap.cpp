/*
 * test_swap.cpp - stage 4b: more processes than slots.  With the RAM disk
 * the kernel swaps processes out to it and in again, and the shell has
 * pipes and the background; without it there are two processes at most,
 * and the shell says so.
 */
#include "Machine.hpp"

#include <string>
#include <vector>

using unixos::Machine;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.runUntil("login:", 1500));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.screen().find("Hello, root") != std::string::npos);
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("$ " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the kernel tells at the boot whether it found the RAM disk") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine with(unixos::readImage(), true);
    REQUIRE(with.runUntil("login:", 1500));
    Machine without(unixos::readImage(), false);
    REQUIRE(without.runUntil("login:", 1500));
    INFO(with.screen());
    INFO(without.screen());
    CHECK(with.screen().find("swap on the RAM disk") != std::string::npos);
    CHECK(without.screen().find("no RAM disk: two processes at most") != std::string::npos);
}

TEST_CASE("a pipe: the first command's output is the second's input") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("echo through a pipe | cat"));
    INFO(m.screen());
    CHECK(after(m, "echo through a pipe | cat").find("\nthrough a pipe\n$") != std::string::npos);
}

TEST_CASE("a pipe of three, four processes on two slots, and a file of several blocks through it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    const bool back = m.command("cat /etc/numbers | cat | cat", 4000);
    INFO(m.screen());
    REQUIRE(back);
    /* The command has scrolled off: the lines on the screen, one after
     * another as the file has them, the last the file's, the prompt after. */
    const std::string text = m.screen();
    std::vector<int> seen;
    for (std::size_t at = text.find("line "); at != std::string::npos; at = text.find("line ", at + 1))
        if (at == 0 || text[at - 1] == '\n') seen.push_back(std::stoi(text.substr(at + 5, 2)));
    REQUIRE(seen.size() >= 20);
    for (std::size_t i = 1; i < seen.size(); ++i) {
        CAPTURE(i);
        CHECK(seen[i] == seen[i - 1] + 1);
    }
    CHECK(seen.back() == 40);
    CHECK(text.find("line 40: the quick brown fox jumps over the lazy dog\n$") != std::string::npos);
    REQUIRE(m.command("echo after"));
    CHECK(after(m, "echo after").find("after\n$") != std::string::npos);
}

TEST_CASE("the background: the shell does not wait, and takes the next command") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    REQUIRE(m.command("echo in the background &"));
    m.run(100);
    REQUIRE(m.command("echo in front"));
    INFO(m.screen());
    CHECK(m.screen().find("in the background") != std::string::npos);
    /* The background's line may come after a prompt, as on any Unix: the
     * command typed then stands on a line of its own. */
    CHECK(m.screen().find("echo in front\nin front\n$") != std::string::npos);
}

TEST_CASE("the shell's children and its own state come back right from the swap") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    for (int i = 0; i < 3; ++i) REQUIRE(m.command("echo x | cat"));
    REQUIRE(m.command("fail"));
    REQUIRE(m.command("status"));
    INFO(m.screen());
    CHECK(after(m, "status").find("status 3") != std::string::npos);
}

TEST_CASE("without the RAM disk a pipe finds no room, and the shell goes on") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("echo x | cat"));
    REQUIRE(m.command("echo still"));
    INFO(m.screen());
    CHECK(m.screen().find("no room for another process") != std::string::npos);
    CHECK(after(m, "echo still").find("still\n$") != std::string::npos);
}

}
