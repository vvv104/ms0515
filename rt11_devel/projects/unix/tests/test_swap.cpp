/*
 * test_swap.cpp - more processes than slots: with swap on - a file, on the
 * RAM disk's volume or on the diskette - the kernel swaps processes out to
 * it and in again, and the shell has pipes and the background; without it
 * there are two processes at most, and the shell says so.
 */
#include "Volume.hpp"

#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.runUntil("login:", 1500));
    m.run(20);
    m.typeAndSettle("root\r", 60);
    REQUIRE(m.screen().find("Hello, root") != std::string::npos);
    REQUIRE(m.waitPrompt());
}

/* Swap on, as /etc/rc turned it on at the boot: the RAM disk a volume at
 * /tmp, a swap file of 256 KB on it. */
void swapOnRam(Machine &m)
{
    REQUIRE(m.screen().find("swap: /tmp/swap") != std::string::npos);
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
    CHECK(with.screen().find("a RAM disk of 512 KB, /dev/rd0") != std::string::npos);
    CHECK(without.screen().find("no RAM disk") != std::string::npos);
}

TEST_CASE("the boot with the RAM disk: its volume and the swap file on it made with no long wait") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    int volume = 0, swap = 0;
    for (; volume < 3000 && m.screen().find("mount: /dev/rd0") == std::string::npos; volume += 5) m.run(5);
    for (; swap < 3000 && m.screen().find("swap: /tmp/swap") == std::string::npos; swap += 5) m.run(5);
    MESSAGE("the RAM disk's volume at frame " << volume << ", the swap file " << swap << " frames after");
    /* 256 KB written, 512 blocks: once 860 frames, with a block, its inode
     * and the bitmap a write; 315 with eight blocks a write, words copied
     * and the RAM disk's bytes four a step. */
    CHECK(swap < 350);
}

TEST_CASE("a pipe: the first command's output is the second's input") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    swapOnRam(m);
    REQUIRE(m.command("echo through a pipe | cat"));
    INFO(m.screen());
    CHECK(after(m, "echo through a pipe | cat").find("\nthrough a pipe\n$") != std::string::npos);
}

TEST_CASE("a pipe of three, four processes on two slots, and a file of several blocks through it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    swapOnRam(m);
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
    swapOnRam(m);
    /* Not waited for: its line may stand under the prompt, the last one. */
    m.typeAndSettle("echo in the background &\r", 100);
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
    swapOnRam(m);
    for (int i = 0; i < 3; ++i) REQUIRE(m.command("echo x | cat"));
    REQUIRE(m.command("fail"));
    REQUIRE(m.command("status"));
    INFO(m.screen());
    CHECK(after(m, "status").find("status 3") != std::string::npos);
}

TEST_CASE("swap on the diskette: a file /swap, the same pipes without the RAM disk") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.screen().find("swap: /swap") != std::string::npos);   /* /etc/rc, no RAM disk */
    REQUIRE(m.command("echo over the diskette | cat", 4000));
    INFO(m.screen());
    CHECK(after(m, "echo over the diskette | cat").find("\nover the diskette\n$") != std::string::npos);
    Volume v(m.imageNow());
    const auto sw = v.lookup("/swap");
    REQUIRE(sw);
    CHECK(v.inode(*sw).nextents == 1);
    CHECK(v.inode(*sw).len[0] == 256);             /* 128 KB: four areas */
}

TEST_CASE("the swap file is held: not removed, its volume not taken out") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    swapOnRam(m);
    REQUIRE(m.command("rm /tmp/swap"));
    REQUIRE(m.command("umount /dev/rd0"));
    REQUIRE(m.command("swapon /tmp/swap"));
    INFO(m.screen());
    CHECK(m.screen().find("rm: /tmp/swap: busy") != std::string::npos);
    CHECK(m.screen().find("umount: /dev/rd0: busy") != std::string::npos);
    CHECK(m.screen().find("swapon: /tmp/swap: busy") != std::string::npos);
}

TEST_CASE("without swap a pipe finds no room, and the shell goes on") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    atPrompt(m);
    REQUIRE(m.command("echo x | cat"));
    REQUIRE(m.command("echo still"));
    INFO(m.screen());
    CHECK(m.screen().find("no room for another process") != std::string::npos);
    CHECK(after(m, "echo still").find("still\n$") != std::string::npos);
}

}
