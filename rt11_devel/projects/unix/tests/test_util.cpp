/*
 * test_util.cpp - the rest of SPEC's text tools and a few of the system's:
 * ls in the names' order; sort (-r, -n, -u), uniq (-c, -d), tr (ranges,
 * -d), cmp (the first difference, -s), od (octal words, -b, -c, -x), find
 * (-name with * and ?, -type), sync.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <sstream>
#include <string>
#include <vector>

using unixos::Machine;

namespace {

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

/* The output of command: its lines between the command and the prompt. */
std::vector<std::string> output(Machine &m, const std::string &command)
{
    REQUIRE(m.command(command, 3000));
    std::string s = after(m, command);
    std::vector<std::string> lines;
    std::istringstream in(s.substr(s.find('\n') + 1));
    for (std::string line; std::getline(in, line) && line.rfind('#', 0) != 0;) {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

using Lines = std::vector<std::string>;

/* A file made with the text typed to cat, Ctrl+D at its end. */
void make(Machine &m, const std::string &path, const std::string &text)
{
    m.typeAndSettle("cat > " + path + "\r", 30);
    m.typeAndSettle(text + "\x04", 30);
    REQUIRE(m.waitPrompt(200));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("ls lists the names in their order") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "ls /") == Lines{"bin  boot  dev  etc  home  mnt  tmp"});
    CHECK(output(m, "ls -a /mnt") == Lines{".  ..  floppy1  floppy2"});
}

TEST_CASE("sort, -r, -n, -u; uniq, -c, -d") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    make(m, "/home/f", "pear\rapple\rfig\rapple\r");
    make(m, "/home/n", "10\r9\r100\r");
    CHECK(output(m, "sort /home/f") == Lines{"apple", "apple", "fig", "pear"});
    CHECK(output(m, "sort -r /home/f") == Lines{"pear", "fig", "apple", "apple"});
    CHECK(output(m, "sort -u /home/f") == Lines{"apple", "fig", "pear"});
    CHECK(output(m, "sort -n /home/n") == Lines{"9", "10", "100"});
    CHECK(output(m, "cat /home/f | sort | uniq") == Lines{"apple", "fig", "pear"});
    CHECK(output(m, "sort /home/f | uniq -c") == Lines{"2 apple", "1 fig", "1 pear"});
    CHECK(output(m, "sort /home/f | uniq -d") == Lines{"apple"});
}

TEST_CASE("tr: a range for a range, a character for a character, -d") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "echo hello | tr a-z A-Z") == Lines{"HELLO"});
    CHECK(output(m, "echo a.b.c | tr . /") == Lines{"a/b/c"});
    CHECK(output(m, "echo hello | tr -d l") == Lines{"heo"});
}

TEST_CASE("cmp: the same is nothing, the first difference told, -s only the status") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    make(m, "/home/a", "one\rtwo\r");
    make(m, "/home/b", "one\rtwx\r");
    make(m, "/home/c", "one\r");
    CHECK(output(m, "cmp /home/a /home/a").empty());
    CHECK(output(m, "cmp /home/a /home/b") == Lines{"/home/a /home/b differ: byte 7, line 2"});
    CHECK(output(m, "cmp /home/a /home/c") == Lines{"cmp: EOF on /home/c"});
    CHECK(output(m, "cmp -s /home/a /home/b").empty());
    CHECK(output(m, "status") == Lines{"status 1"});
}

TEST_CASE("od: octal words, -b bytes, -c characters, -x hex") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "echo AB | od") == Lines{"0000000 041101 000012", "0000003"});
    CHECK(output(m, "echo AB | od -b") == Lines{"0000000 101 102 012", "0000003"});
    CHECK(output(m, "echo AB | od -c") == Lines{"0000000   A   B  \\n", "0000003"});
    CHECK(output(m, "echo AB | od -x") == Lines{"0000000 4241 000a", "0000003"});
}

TEST_CASE("find: every path under, -name with * and ?, -type") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "find /mnt") == Lines{"/mnt", "/mnt/floppy1", "/mnt/floppy2"});
    CHECK(output(m, "find / -name motd") == Lines{"/etc/motd"});
    CHECK(output(m, "find /etc -name f*") == Lines{"/etc/fstab"});
    CHECK(output(m, "find /mnt -name floppy?") == Lines{"/mnt/floppy1", "/mnt/floppy2"});
    CHECK(output(m, "find / -type d -name m*") == Lines{"/mnt"});
    CHECK(output(m, "find /boot -type f -name ??") == Lines{"/boot/fd", "/boot/rd"});
    REQUIRE(m.command("sync"));
    CHECK(output(m, "status") == Lines{"status 0"});
}

}
