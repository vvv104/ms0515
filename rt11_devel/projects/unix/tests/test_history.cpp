/*
 * test_history.cpp - the shell's history by the arrows.  The ROM gives an
 * arrow as VT52's ESC and a letter; while the shell reads its line the
 * kernel hands the line over at up or down, as at Tab, and the shell puts
 * the line it remembers in its place - Ctrl+U, the line wiped, then the
 * text, typed into the console's line (TTY_TAB).  Up goes back, down
 * forward to the empty line; a line called up is edited and run as any.
 * The other escapes, and the arrows for a program that reads the console,
 * are nothing.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <ms0515/Emulator.hpp>

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

std::string lastLine(Machine &m)
{
    std::string s = m.shown();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    const auto nl = s.rfind('\n');
    return s.substr(nl == std::string::npos ? 0 : nl + 1);
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

void press(Machine &m, ms0515::Key key)
{
    m.tapBare(key);
    m.run(30);
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("up and down walk the lines typed; one called up is run again") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo one"));
    REQUIRE(m.command("echo two"));
    press(m, ms0515::Key::Up);
    CHECK(lastLine(m) == "/home# echo two");
    press(m, ms0515::Key::Up);
    CHECK(lastLine(m) == "/home# echo one");
    press(m, ms0515::Key::Up);                               /* the oldest: it stays */
    CHECK(lastLine(m) == "/home# echo one");
    press(m, ms0515::Key::Down);
    CHECK(lastLine(m) == "/home# echo two");
    press(m, ms0515::Key::Down);
    CHECK(lastLine(m) == "/home#");
    press(m, ms0515::Key::Up);
    REQUIRE(m.command(""));
    INFO(m.screen());
    CHECK(after(m, "echo two").find("\ntwo\n#") != std::string::npos);
}

TEST_CASE("a line called up is edited, BS and more, and run") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    REQUIRE(m.command("echo two"));
    press(m, ms0515::Key::Up);
    REQUIRE(m.command("\bx"));
    INFO(m.screen());
    CHECK(after(m, "echo twx").find("\ntwx\n#") != std::string::npos);
    press(m, ms0515::Key::Up);                               /* the edited one is the last now */
    CHECK(lastLine(m) == "/home# echo twx");
}

TEST_CASE("Ctrl+U wipes the line; the other arrows are nothing") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("garbage\x15", 30);
    CHECK(lastLine(m) == "/home#");
    press(m, ms0515::Key::Left);
    press(m, ms0515::Key::Right);
    REQUIRE(m.command("echo ok"));
    INFO(m.screen());
    CHECK(after(m, "echo ok").find("\nok\n#") != std::string::npos);
}

TEST_CASE("the arrows are nothing to a program reading the console") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    m.typeAndSettle("cat > /home/k\r", 30);
    m.typeAndSettle("a", 10);
    press(m, ms0515::Key::Up);
    press(m, ms0515::Key::Down);
    m.typeAndSettle("b\r\x04", 100);
    REQUIRE(m.waitPrompt(200));
    Volume v(m.imageNow());
    const auto k = v.lookup("/home/k");
    REQUIRE(k);
    CHECK(v.content(v.inode(*k)) == "ab\n");
}

TEST_CASE("many long lines: the oldest go, the newest are there") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    const std::string pad(60, 'x');
    for (int i = 0; i < 6; ++i) REQUIRE(m.command("echo " + std::to_string(i) + pad));
    press(m, ms0515::Key::Up);
    CHECK(lastLine(m) == "/home# echo 5" + pad);
    for (int i = 0; i < 8; ++i) press(m, ms0515::Key::Up);
    const std::string oldest = lastLine(m);
    INFO(oldest);
    CHECK(oldest.rfind("/home# echo ", 0) == 0);
    CHECK(oldest != "/home# echo 0" + pad);                   /* gone: 192 bytes hold some */
    REQUIRE(m.command(""));
    CHECK(m.screen().find("panic") == std::string::npos);
}

}
