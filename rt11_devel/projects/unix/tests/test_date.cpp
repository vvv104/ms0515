/*
 * test_date.cpp - the date and the files' times.  The machine has no
 * calendar clock: the kernel counts from 1970 at the boot until date sets
 * it (stime); a file written takes the time then, ls -l shows it, and the
 * images' files carry the time they were built.
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

/* A file's mtime as the volume has it: the inode's long at 8. */
unsigned long mtime(const std::vector<uint8_t> &image, const char *path)
{
    Volume v(image);
    const auto n = v.lookup(path);
    REQUIRE(n);
    const int block = static_cast<int>(v.word(1, 16) + (*n - 1) / 16);
    return v.longAt(block, static_cast<int>((*n - 1) % 16 * 32 + 8));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("date: 1970 until it is set, then the date set, going on") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    auto out = output(m, "date");
    REQUIRE(out.size() == 1);
    CHECK(out[0].rfind("Thu Jan  1 00:", 0) == 0);
    CHECK(out[0].substr(out[0].size() - 4) == "1970");
    REQUIRE(m.command("date 2610171642"));
    CHECK(after(m, "date 2610171642").find("\nSat Oct 17 16:42:0") != std::string::npos);
    m.run(150);                                               /* three seconds on */
    out = output(m, "date");
    REQUIRE(out.size() == 1);
    CHECK(out[0].rfind("Sat Oct 17 16:42:0", 0) == 0);
    CHECK(out[0] != "Sat Oct 17 16:42:00 2026");
    CHECK(out[0].substr(out[0].size() - 4) == "2026");
}

TEST_CASE("date refuses what is not a date, and the clock stays") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "date 99") == std::vector<std::string>{"usage: date [YYMMDDhhmm[.ss]]"});
    CHECK(output(m, "date 2613011200") == std::vector<std::string>{"usage: date [YYMMDDhhmm[.ss]]"});
    CHECK(output(m, "status") == std::vector<std::string>{"status 1"});
    REQUIRE(m.command("date"));
    CHECK(after(m, "date").find("1970") != std::string::npos);
}

TEST_CASE("a file written takes the time; ls -l shows it; the image's files their build's") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    const auto image = unixos::readImage();
    CHECK(mtime(image, "/bin/sh") > 1700000000ul);            /* built after 2023 */
    Machine m(image, false);
    REQUIRE(m.boot());
    REQUIRE(m.command("date 2610171642.30"));
    REQUIRE(m.command("echo x > /home/f"));
    REQUIRE(m.command("mkdir /home/d"));
    const unsigned long want = 1792255350ul;                  /* 2026-10-17 16:42:30 */
    const unsigned long f = mtime(m.imageNow(), "/home/f"), d = mtime(m.imageNow(), "/home/d");
    CHECK(f >= want);
    CHECK(f < want + 30);
    CHECK(d >= want);
    const auto ls = output(m, "ls -l /home");
    INFO(m.screen());
    bool shown = false;
    for (const auto &l : ls)
        if (l.size() > 2 && l.substr(l.size() - 2) == " f") shown = l.find("Oct 17 2026 16:42") != std::string::npos;
    CHECK(shown);
}

}
