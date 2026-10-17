/*
 * test_pipes.cpp - two slots and no swap.  A pipeline runs one command
 * after another, each one's output a file in /tmp the next one reads, the
 * files gone after.  Two commands of one's own at once: with both slots
 * taken a fork parks the shell - its used part, a few KB - in the shadow
 * copy of bank 6, the child in its slot, and the shell comes back when a
 * slot is free.
 */
#include "Volume.hpp"

#include <string>
#include <vector>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the kernel tells at the boot whether it found the RAM disk") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine with(unixos::readImage(), true);
    REQUIRE(with.boot(1500));
    Machine without(unixos::readImage(), false);
    REQUIRE(without.boot(1500));
    INFO(with.screen());
    INFO(without.screen());
    CHECK(with.screen().find("a RAM disk of 512 KB, /dev/rd0") != std::string::npos);
    CHECK(without.screen().find("no RAM disk") != std::string::npos);
}

TEST_CASE("no swap: no swap file on the volume, no swapon") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    INFO(m.screen());
    CHECK(m.screen().find("swap") == std::string::npos);
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/swap"));
    CHECK_FALSE(v.lookup("/bin/swapon"));
    Volume rd(m.ramDisk(), true);
    CHECK_FALSE(rd.lookup("/swap"));
}

TEST_CASE("a pipe: the first command's output is the second's input, the files in /tmp gone after") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo through a pipe | cat"));
    REQUIRE(m.command("ls -a /tmp"));
    INFO(m.screen());
    CHECK(after(m, "echo through a pipe | cat").find("\nthrough a pipe\n#") != std::string::npos);
    CHECK(after(m, "ls -a /tmp").find(".  ..\n#") != std::string::npos);
}

TEST_CASE("a pipeline of three carries a file of several blocks, line by line") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
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
    CHECK(text.find("line 40: the quick brown fox jumps over the lazy dog\n#") != std::string::npos);
}

TEST_CASE("without the RAM disk the pipe goes through the diskette's /tmp, and a stage's own redirection holds") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readBareImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("cat < /etc/motd | cat > /home/m", 3000));
    REQUIRE(m.command("echo x | cat"));
    INFO(m.screen());
    CHECK(after(m, "echo x | cat").find("\nx\n#") != std::string::npos);
    Volume v(m.imageNow());
    const auto motd = v.lookup("/etc/motd"), copy = v.lookup("/home/m");
    REQUIRE(motd);
    REQUIRE(copy);
    CHECK(v.content(v.inode(*copy)) == v.content(v.inode(*motd)));
}

TEST_CASE("the background: the shell does not wait, and takes the next command") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    /* Not waited for: its line may stand under the prompt, the last one. */
    m.typeAndSettle("echo in the background &\r", 100);
    REQUIRE(m.command("echo in front"));
    INFO(m.screen());
    CHECK(m.screen().find("in the background") != std::string::npos);
    CHECK(m.screen().find("echo in front\nin front\n#") != std::string::npos);
}

TEST_CASE("two commands of one's own at once: one in the background, one in front, the shell parked") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("cd /etc"));
    m.typeAndSettle("sleep 4 &\r", 50);
    REQUIRE(m.waitPrompt());
    const bool back = m.command("cat motd");                  /* the shell parked for it, and back */
    INFO(m.screen());
    REQUIRE(back);
    CHECK(after(m, "cat motd").find("Welcome to UNIX") != std::string::npos);
    CHECK(m.screen().find("no room") == std::string::npos);
    REQUIRE(m.command("pwd"));                                /* its own state came back with it */
    CHECK(after(m, "pwd").find("\n/etc\n#") != std::string::npos);
    REQUIRE(m.command("echo still"));
    CHECK(after(m, "echo still").find("\nstill\n#") != std::string::npos);
}

TEST_CASE("the shell stays small enough to park: its program at most 7 KB, the stack's room left of 8") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Volume v(unixos::readImage());
    const auto sh = v.lookup("/bin/sh");
    REQUIRE(sh);
    const std::string a = v.content(v.inode(*sh));
    REQUIRE(a.size() >= 16);
    const auto word = [&](int i) { return static_cast<unsigned>(static_cast<uint8_t>(a[2 * i]) | static_cast<uint8_t>(a[2 * i + 1]) << 8); };
    const unsigned size = word(1) + word(2) + word(3);       /* text, data, bss */
    MESSAGE("the shell: " << size << " bytes");
    CHECK(size <= 7 * 1024);
}

TEST_CASE("two in the background: the shell waits parked until one of them ends") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("sleep 2 &\r", 50);
    REQUIRE(m.waitPrompt());
    m.typeAndSettle("sleep 2 &\r", 50);
    REQUIRE(m.waitPrompt(1000));                              /* when the first is gone */
    REQUIRE(m.command("echo after"));
    INFO(m.screen());
    CHECK(m.screen().find("no room") == std::string::npos);
    CHECK(after(m, "echo after").find("\nafter\n#") != std::string::npos);
}

TEST_CASE("the processor shared: with a busy one in the background the shell answers at once") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("sleep 8 &\r", 50);
    REQUIRE(m.waitPrompt());
    int frames = 0;
    m.typeAndSettle("t\r", 0);
    for (; frames < 400 && m.screen().find("\ntime ") == std::string::npos; frames += 5) m.run(5);
    INFO(m.screen());
    MESSAGE("the shell answered in " << frames << " frames");
    CHECK(frames <= 50);                                      /* a second, not the sleep's eight */
}

TEST_CASE("sleep N: N seconds by the clock at the least, not a second more") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    int frames = 0;
    m.typeAndSettle("sleep 2\r", 0);
    for (; frames < 500 && !m.waitPrompt(5); frames += 5) {}
    MESSAGE("sleep 2 took " << frames << " frames");
    CHECK(frames >= 100);                                     /* 50 frames a second */
    CHECK(frames <= 165);
}

TEST_CASE("a pipeline is not put in the background: one command at a time in front") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo a | cat &"));
    REQUIRE(m.command("echo still"));
    INFO(m.screen());
    CHECK(after(m, "echo a | cat &").find("sh: a pipeline in the background: no room") != std::string::npos);
    CHECK(after(m, "echo still").find("\nstill\n#") != std::string::npos);
}

TEST_CASE("the exit status of a pipeline is its last command's, the shell's own state kept") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    REQUIRE(m.boot());
    for (int i = 0; i < 3; ++i) REQUIRE(m.command("echo x | cat"));
    REQUIRE(m.command("echo x | fail"));
    REQUIRE(m.command("status"));
    INFO(m.screen());
    CHECK(after(m, "status").find("status 3") != std::string::npos);
}

}
