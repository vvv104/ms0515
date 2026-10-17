/*
 * test_ed.cpp - /bin/ed, the line editor: a file made and written, lines
 * printed by number, by . $ + -, by /pattern/ and ?pattern?, substituted,
 * deleted, changed, inserted, joined, moved and copied, other files read
 * and written, and its ? for what it cannot do; q not quitting over what
 * is not written until it is said twice.  Its commands from a file as
 * well as from the console.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <initializer_list>
#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

/* Lines typed to ed, frames each for it to answer: ed itself loaded, a
 * file read or written, take the longest. */
void type(Machine &m, std::initializer_list<const char *> lines)
{
    for (const char *l : lines) {
        const std::string s(l);
        const bool slow = s.rfind("ed ", 0) == 0 || s[0] == 'w' || s[0] == 'r' || s[0] == 'e' ||
                          s.find('w') != std::string::npos;
        m.typeAndSettle(s + "\r", slow ? 300 : 30);
    }
}

std::string file(Machine &m, const char *path)
{
    Volume v(m.imageNow());
    const auto n = v.lookup(path);
    return n ? v.content(v.inode(*n)) : std::string("<none>");
}

/* The screen from the last time `from` stands on it. */
std::string since(Machine &m, const std::string &from)
{
    const std::string s = m.screen();
    const auto at = s.rfind(from);
    return at == std::string::npos ? std::string() : s.substr(at + from.size());
}

void edit(Machine &m, const char *path, const char *text)
{
    m.typeAndSettle(std::string("cat > ") + path + "\r", 30);
    m.typeAndSettle(std::string(text) + "\x04", 30);
    REQUIRE(m.waitPrompt(200));
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("ed makes a file: a, the lines, ., w with the bytes, q") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    type(m, {"ed /home/t", "a", "one", "two", "three", ".", "w"});
    INFO(m.screen());
    CHECK(since(m, "\nw\n").rfind("14", 0) == 0);
    type(m, {"q"});
    REQUIRE(m.waitPrompt(100));
    CHECK(file(m, "/home/t") == "one\ntwo\nthree\n");
}

TEST_CASE("ed prints by number, . $ + -, a pattern either way, Enter for the next, = and n") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    edit(m, "/home/t", "alpha\rbeta\rgamma\rdelta\r");
    type(m, {"ed /home/t"});
    CHECK(since(m, "ed /home/t\n").rfind("23", 0) == 0);     /* the bytes read */
    type(m, {"2p"});
    CHECK(since(m, "\n2p\n").rfind("beta\n", 0) == 0);
    type(m, {"$p"});
    CHECK(since(m, "\n$p\n").rfind("delta\n", 0) == 0);
    type(m, {"-p"});
    CHECK(since(m, "\n-p\n").rfind("gamma\n", 0) == 0);
    type(m, {"/al/"});
    CHECK(since(m, "\n/al/\n").rfind("alpha\n", 0) == 0);
    type(m, {""});
    CHECK(since(m, "alpha\n\n").rfind("beta\n", 0) == 0);
    type(m, {"?ph?"});
    CHECK(since(m, "\n?ph?\n").rfind("alpha\n", 0) == 0);
    type(m, {"$="});
    CHECK(since(m, "\n$=\n").rfind("4\n", 0) == 0);
    type(m, {"2,3n"});
    {
        const std::string n = since(m, "\n2,3n\n");            /* the tab drawn as spaces */
        CHECK(n.rfind("2 ", 0) == 0);
        CHECK(n.find("beta\n3 ") != std::string::npos);
        CHECK(n.find("gamma\n") != std::string::npos);
    }
    type(m, {",p"});
    CHECK(since(m, "\n,p\n").rfind("alpha\nbeta\ngamma\ndelta\n", 0) == 0);
    type(m, {"q"});
    REQUIRE(m.waitPrompt(100));
}

TEST_CASE("ed substitutes: the first, g for all, & the match, a range, the last pattern") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    edit(m, "/home/t", "a cat sat\ron a mat\rno match\r");
    type(m, {"ed /home/t", "1s/at/og/", "2s/a/[&]/g", "1,2s/^/> /", "/no/s//yes/", "w", "q"});
    REQUIRE(m.waitPrompt(100));
    INFO(m.screen());
    CHECK(file(m, "/home/t") == "> a cog sat\n> on [a] m[a]t\nyes match\n");
}

TEST_CASE("ed deletes, changes, inserts, joins, moves and copies") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    edit(m, "/home/t", "1\r2\r3\r4\r5\r");
    type(m, {"ed /home/t", "2d", "3c", "three", ".", "1i", "zero", ".", "4,5j", "1m$", "1t0", "w", "q"});
    REQUIRE(m.waitPrompt(100));
    INFO(m.screen());
    /* 1 3 4 5 -> 1 3 three 5 -> zero 1 3 three 5 -> zero 1 3 three5
     * -> 1 3 three5 zero -> 1 1 3 three5 zero */
    CHECK(file(m, "/home/t") == "1\n1\n3\nthree5\nzero\n");
}

TEST_CASE("ed reads and writes other files, e and f; q twice over what is not written") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    edit(m, "/home/t", "first\r");
    type(m, {"ed /home/t", "r /etc/motd", "1,2w /home/u", "f", "e /home/u", "f /home/v", "w", "a", "more", ".", "q"});
    INFO(m.screen());
    CHECK(since(m, "\nf\n").rfind("/home/t\n", 0) == 0);
    CHECK(since(m, "\nq\n").rfind("?", 0) == 0);            /* not written */
    type(m, {"q"});
    REQUIRE(m.waitPrompt(100));
    const std::string motd = file(m, "/etc/motd");
    const std::string two = "first\n" + motd.substr(0, motd.find('\n') + 1);   /* lines 1 and 2 */
    CHECK(file(m, "/home/u") == two);
    CHECK(file(m, "/home/v") == two);
    CHECK(file(m, "/home/t") == "first\n");
}

TEST_CASE("ed's ?: a command it has not, a line not there, a file it may not write; Q") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    type(m, {"ed /bin/ls", "zz"});
    CHECK(since(m, "\nzz\n").rfind("?", 0) == 0);
    type(m, {"99999p"});
    CHECK(since(m, "\n99999p\n").rfind("?", 0) == 0);
    type(m, {"/no such thing/"});
    CHECK(since(m, "\n/no such thing/\n").rfind("?", 0) == 0);
    type(m, {"1d", "w"});
    CHECK(since(m, "\n1d\nw\n").rfind("?", 0) == 0);        /* /bin/ls is r-x */
    type(m, {"Q"});
    REQUIRE(m.waitPrompt(100));
    INFO(m.screen());
    REQUIRE(m.command("ls /bin"));
    CHECK(m.screen().find("panic") == std::string::npos);
}

TEST_CASE("ed takes its commands from a file") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    REQUIRE(m.boot());
    edit(m, "/home/t", "red\rgreen\r");
    edit(m, "/home/cmds", "1s/red/blue/\r$a\ryellow\r.\rw\rq\r");
    REQUIRE(m.command("ed /home/t < /home/cmds", 300));
    INFO(m.screen());
    CHECK(file(m, "/home/t") == "blue\ngreen\nyellow\n");
}

}
