/*
 * test_fs.cpp - stage 3: the file system.  The image as mkfs.py made it,
 * read on the host; and on the machine, the kernel's open, read and exec
 * through the shell's commands.
 */
#include "Volume.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string hostFile(const std::string &path)
{
    std::ifstream in(std::string{UNIX_ROOT} + path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The machine booted, at the shell's prompt. */
void loggedIn(Machine &m)
{
    REQUIRE(m.boot(1500));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the volume: the superblock, the areas, the root's directories, a file as the host has it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Volume v(unixos::readImage());
    CHECK(v.word(1, 0) == 054125);                 /* the magic */
    CHECK(v.word(1, 2) == 2);                      /* the version */
    CHECK(v.word(1, 4) == 1600);                   /* blocks */
    CHECK(v.word(1, 14) == 2);                     /* the bitmap */
    CHECK(v.word(1, 16) == 3);                     /* the inode table, 8 blocks */
    CHECK(v.word(1, 18) == 8);
    CHECK(v.word(1, 24) == 11);                    /* the data: no swap area */
    CHECK(v.word(1, 26) == 128);                   /* inodes */
    const auto root = v.inode(1);
    CHECK((root.mode & 0170000) == 0040000);
    std::vector<std::string> names;
    for (const auto &e : v.entries(1)) names.push_back(e.first);
    for (const char *n : {".", "..", "boot", "etc", "bin"}) {
        CAPTURE(n);
        CHECK(std::find(names.begin(), names.end(), n) != names.end());
    }
    const auto motd = v.lookup("/etc/motd");
    REQUIRE(motd);
    CHECK(v.content(v.inode(*motd)) == hostFile("/etc/motd"));
    const auto numbers = v.lookup("/etc/numbers");
    REQUIRE(numbers);
    CHECK(v.inode(*numbers).size > 1024);          /* more than two blocks */
    CHECK(v.content(v.inode(*numbers)) == hostFile("/etc/numbers"));
}

TEST_CASE("the kernel is /boot/kernel, and the superblock names its blocks for the boot block") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Volume v(unixos::readImage());
    const auto k = v.lookup("/boot/kernel");
    REQUIRE(k);
    const auto ki = v.inode(*k);
    CHECK(ki.nextents == 1);
    CHECK(v.word(1, 6) == ki.start[0]);
    CHECK(v.word(1, 8) == ki.len[0]);
    CHECK(v.word(1, 10) == 01000);                 /* loaded and entered at 01000 */
    CHECK(v.word(1, 12) == 01000);
    const auto init = v.lookup("/etc/init");
    REQUIRE(init);
    CHECK(v.word(v.inode(*init).start[0], 0) == 0407);   /* an a.out of the 0407 kind */
}

TEST_CASE("cat shows a file, ls a directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    loggedIn(m);
    m.command("cat /etc/motd");
    m.command("ls /");
    m.command("ls /bin");
    INFO(m.screen());
    CHECK(m.screen().find("Welcome to UBIX on the Elektronika MS 0515.") != std::string::npos);
    for (const char *n : {"boot", "etc", "bin", "dev", "home", "tmp"}) {
        CAPTURE(n);
        CHECK(m.screen().find(n) != std::string::npos);
    }
    CHECK(m.screen().find("  echo  ") != std::string::npos);
    CHECK(m.screen().find("hello  ") != std::string::npos);
}

TEST_CASE("a file of several blocks is read whole, across the blocks") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    loggedIn(m);
    m.typeAndSettle("cat /etc/numbers\r", 0);
    REQUIRE(m.runUntil("line 40: the quick brown fox jumps over the lazy dog", 3000));
    m.run(50);                                     /* the prompt after it, the scrolling over */
    INFO(m.screen());
    /* The lines on the screen, one after another as the file has them,
     * none lost or doubled at the blocks' edges, the last the file's. */
    std::vector<int> seen;
    const std::string text = m.screen();
    for (std::size_t at = text.find("line "); at != std::string::npos; at = text.find("line ", at + 1))
        if (at == 0 || text[at - 1] == '\n')
            seen.push_back(std::stoi(text.substr(at + 5, 2)));
    REQUIRE(seen.size() >= 20);
    for (std::size_t i = 1; i < seen.size(); ++i) {
        CAPTURE(i);
        CHECK(seen[i] == seen[i - 1] + 1);
    }
    CHECK(seen.back() == 40);
}

TEST_CASE("a file that is not there is reported") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    loggedIn(m);
    m.command("cat /etc/nothing");
    m.command("ls /nowhere");
    m.command("/bin/nope");
    INFO(m.screen());
    CHECK(m.screen().find("cat: /etc/nothing: not found") != std::string::npos);
    CHECK(m.screen().find("ls: /nowhere: not found") != std::string::npos);
    CHECK(m.screen().find("/bin/nope: not found") != std::string::npos);
}

TEST_CASE("a program read off the volume by exec gets its arguments") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage());
    loggedIn(m);
    m.command("/bin/hello");
    m.command("/bin/echo one two  three");
    INFO(m.screen());
    CHECK(m.screen().find("hello, world") != std::string::npos);
    CHECK(m.screen().find("one two three") != std::string::npos);
}

}
