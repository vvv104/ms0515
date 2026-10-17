/*
 * test_ps.cpp - ps and kill.  ps lists the processes - pid, parent, state
 * (R running, S asleep, Z gone but not waited for), & for the background,
 * the name its argv[0] gave; kill PID takes one out as Ctrl+C does, a
 * sleeping one woken to it; not the session's shell, not one not there.
 */
#include "Machine.hpp"

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

/* The line of ps for the name, empty for none. */
std::string row(const std::vector<std::string> &ps, const std::string &name)
{
    for (const auto &l : ps)
        if (l.size() >= name.size() && l.compare(l.size() - name.size(), name.size(), name) == 0) return l;
    return {};
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("ps: the shell and ps itself, a header first") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    const auto ps = output(m, "ps");
    INFO(m.screen());
    REQUIRE(ps.size() == 3);
    CHECK(ps[0] == "  PID  PPID S  NAME");
    CHECK(row(ps, " sh") == "    1     0 S  sh");
    CHECK(row(ps, " ps").find(" 1 R  ps") != std::string::npos);
}

TEST_CASE("kill takes out a command in the background; ps shows it & before, gone after") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("sleep 60 &\r", 50);
    REQUIRE(m.waitPrompt());
    auto ps = output(m, "ps");
    INFO(m.screen());
    const std::string sleep = row(ps, " sleep");
    REQUIRE(!sleep.empty());
    CHECK(sleep.find("&") != std::string::npos);
    const std::string pid = sleep.substr(0, 5);
    REQUIRE(m.command("kill " + pid));
    CHECK(after(m, "kill " + pid).find("kill:") == std::string::npos);
    m.run(50);
    ps = output(m, "ps");
    CHECK(row(ps, " sleep").empty());
}

TEST_CASE("kill refused: the session's shell, one not there, no number") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    CHECK(output(m, "kill 1") == std::vector<std::string>{"kill: 1: not allowed"});
    CHECK(output(m, "kill 99") == std::vector<std::string>{"kill: 99: no such process"});
    CHECK(output(m, "kill x") == std::vector<std::string>{"usage: kill PID..."});
    REQUIRE(m.command("echo alive"));
    CHECK(after(m, "echo alive").find("\nalive\n#") != std::string::npos);
}

}
