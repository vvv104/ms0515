/*
 * test_pipes.cpp - two processes at once, one in each slot, and no swap:
 * the shell and a command.  A pipeline runs one command after another,
 * each one's output a file in /tmp the next one reads, the files gone
 * after; the background takes the other slot.
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
