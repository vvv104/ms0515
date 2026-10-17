/*
 * test_text.cpp - the text tools and rmdir.  wc counts a file's lines,
 * words and bytes; head and tail give its first and its last lines, ten
 * or -N; grep the lines that hold a pattern - ^ and $ at the ends, . any
 * character, * any number of the one before - or with -v those that do
 * not, -n numbered, -c counted; each reads its input with no file.
 * rmdir takes away an empty directory, not one with names in it, nor one
 * in use; rm leaves directories alone.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <sstream>
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

/* /etc/numbers as the volume has it, line by line. */
std::vector<std::string> numbers()
{
    Volume v(unixos::readImage());
    const auto n = v.lookup("/etc/numbers");
    REQUIRE(n);
    std::vector<std::string> lines;
    std::istringstream in(v.content(v.inode(*n)));
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
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

int taken(const std::vector<uint8_t> &image)
{
    Volume v(image);
    const int bitmap = static_cast<int>(v.word(1, 14));
    int n = 0;
    for (int b = 0; b < 1600; ++b) n += (v.word(bitmap, b / 16 * 2) >> (b % 16)) & 1;
    return n;
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("wc: lines, words, bytes; of a file and of its input") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto lines = numbers();
    std::size_t words = 0, bytes = 0;
    for (const auto &l : lines) {
        std::istringstream w(l);
        for (std::string x; w >> x;) ++words;
        bytes += l.size() + 1;
    }
    const std::string want = std::to_string(lines.size()) + " " + std::to_string(words) + " " + std::to_string(bytes);
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    auto out = output(m, "wc /etc/numbers");
    REQUIRE(out.size() == 1);
    CHECK(out[0] == want + " /etc/numbers");
    out = output(m, "cat /etc/numbers | wc");
    REQUIRE(out.size() == 1);
    CHECK(out[0] == want);
}

TEST_CASE("head and tail: ten lines, or -N; a file or the input") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto lines = numbers();
    REQUIRE(lines.size() == 40);
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "head -3 /etc/numbers") == std::vector<std::string>(lines.begin(), lines.begin() + 3));
    CHECK(output(m, "tail -2 /etc/numbers") == std::vector<std::string>(lines.end() - 2, lines.end()));
    CHECK(output(m, "cat /etc/numbers | tail -1") == std::vector<std::string>(lines.end() - 1, lines.end()));
    CHECK(output(m, "head /etc/numbers").size() == 10);
    CHECK(output(m, "tail /etc/numbers") == std::vector<std::string>(lines.end() - 10, lines.end()));
    const auto all = output(m, "head -50 /etc/numbers | wc");  /* more than the screen holds */
    REQUIRE(all.size() == 1);
    CHECK(all[0].rfind("40 ", 0) == 0);
}

TEST_CASE("grep: a pattern's lines; ^ $ . *; -v, -n, -c") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto lines = numbers();
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    auto out = output(m, "grep 0: /etc/numbers");
    CHECK(out == std::vector<std::string>{lines[9], lines[19], lines[29], lines[39]});
    CHECK(output(m, "grep -c ^line.0 /etc/numbers") == std::vector<std::string>{"9"});
    CHECK(output(m, "grep -c dog$ /etc/numbers") == std::vector<std::string>{"40"});
    CHECK(output(m, "grep -c fox.*dog /etc/numbers") == std::vector<std::string>{"40"});
    CHECK(output(m, "grep -c ^dog /etc/numbers") == std::vector<std::string>{"0"});
    CHECK(output(m, "grep -v 1 /etc/numbers | grep -c 4") == std::vector<std::string>{"4"});   /* 4: 04 24 34 40 */
    out = output(m, "grep -n 40: /etc/numbers");
    CHECK(out == std::vector<std::string>{"40:" + lines[39]});
    CHECK(output(m, "cat /etc/numbers | grep -c x*") == std::vector<std::string>{"40"});
    REQUIRE(m.command("grep fox /etc/none"));
    CHECK(after(m, "grep fox /etc/none").find("grep: /etc/none: not found") != std::string::npos);
    REQUIRE(m.command("status"));
    CHECK(after(m, "status").find("status 2") != std::string::npos);
    REQUIRE(m.command("grep cat /etc/numbers"));
    REQUIRE(m.command("status"));
    CHECK(after(m, "status").find("status 1") != std::string::npos);
}

TEST_CASE("rmdir: an empty directory goes, its blocks and its parent's link with it") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    const int before = taken(m.imageNow());
    REQUIRE(m.command("mkdir /home/d"));
    REQUIRE(m.command("rmdir /home/d"));
    INFO(m.screen());
    CHECK(after(m, "rmdir /home/d").find("rmdir:") == std::string::npos);
    Volume v(m.imageNow());
    CHECK_FALSE(v.lookup("/home/d"));
    CHECK(v.inode(*v.lookup("/home")).nlink == 2);
    CHECK(taken(m.imageNow()) == before);
    REQUIRE(m.command("mkdir /home/d"));                     /* the name is free again */
    CHECK(Volume(m.imageNow()).lookup("/home/d"));
}

TEST_CASE("rmdir refused: names in it, a file, one in use, the root, rm of a directory") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("mkdir /home/d"));
    REQUIRE(m.command("echo x > /home/d/f"));
    REQUIRE(m.command("rmdir /home/d"));
    CHECK(after(m, "rmdir /home/d").find("rmdir: /home/d: not empty") != std::string::npos);
    REQUIRE(m.command("rmdir /home/d/f"));
    CHECK(after(m, "rmdir /home/d/f").find("rmdir: /home/d/f: not a directory") != std::string::npos);
    REQUIRE(m.command("rm /home/d"));
    CHECK(after(m, "rm /home/d").find("rm: /home/d: a directory") != std::string::npos);
    REQUIRE(m.command("mkdir /home/e"));
    REQUIRE(m.command("cd /home/e"));
    REQUIRE(m.command("rmdir /home/e"));
    CHECK(after(m, "rmdir /home/e").find("rmdir: /home/e: busy") != std::string::npos);
    REQUIRE(m.command("rmdir /"));
    CHECK(after(m, "rmdir /").find("rmdir: /: ") != std::string::npos);
    REQUIRE(m.command("cd /"));
    REQUIRE(m.command("rmdir /home/e"));
    INFO(m.screen());
    CHECK(after(m, "rmdir /home/e").find("rmdir:") == std::string::npos);
    CHECK(Volume(m.imageNow()).lookup("/home/d/f"));
}

}
