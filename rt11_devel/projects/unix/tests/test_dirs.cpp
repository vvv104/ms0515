/*
 * test_dirs.cpp - a process's own directory: chdir, the shell's cd, paths
 * from it when they do not begin with /, its children's the same; pwd by
 * stat, walking .. up to the root, out of a mounted volume too.  The
 * session's shell starts in /home, and cd alone goes back there.
 */
#include "Machine.hpp"
#include "Volume.hpp"

#include <string>

using unixos::Machine;
using unixos::Volume;

namespace {

void atPrompt(Machine &m)
{
    REQUIRE(m.boot(3000));
}

std::string after(Machine &m, const std::string &command)
{
    const std::string s = m.screen();
    const auto at = s.rfind("# " + command);
    return at == std::string::npos ? std::string() : s.substr(at + 2 + command.size());
}

void says(Machine &m, const std::string &command, const std::string &text)
{
    REQUIRE(m.command(command, 3000));
    INFO(m.screen());
    CAPTURE(command);
    CHECK(after(m, command).find("\n" + text + "\n#") != std::string::npos);
}

} // namespace

TEST_SUITE("UNIX") {

TEST_CASE("the session starts in /home; cd alone goes there; a script typed runs in the shell, its cd kept") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);
    atPrompt(m);
    says(m, "pwd", "/home");
    REQUIRE(m.command("echo mine > notes"));
    REQUIRE(m.command("cd /etc"));
    REQUIRE(m.command("cd"));
    says(m, "pwd", "/home");
    says(m, "cat /home/notes", "mine");
    REQUIRE(m.command("cd /etc"));
    REQUIRE(m.command("echo pwd > /tmp/p"));
    says(m, "sh /tmp/p", "/etc");
    REQUIRE(m.command("echo cd /bin > /tmp/c"));
    REQUIRE(m.command("sh /tmp/c"));                          /* in this shell: no slot for another */
    says(m, "pwd", "/bin");
}

TEST_CASE("cd and pwd: paths from a process's own directory, .. up, its children's the same") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    REQUIRE(m.command("mkdir /d1"));
    REQUIRE(m.command("mkdir /d1/d2"));
    REQUIRE(m.command("cd /d1/d2"));
    says(m, "pwd", "/d1/d2");
    REQUIRE(m.command("echo here > f"));
    says(m, "cat /d1/d2/f", "here");
    says(m, "cat f | cat", "here");                   /* the children are where the shell is */
    REQUIRE(m.command("cd .."));
    says(m, "pwd", "/d1");
    says(m, "ls", "d2");
    says(m, "cat d2/f", "here");
    REQUIRE(m.command("cd /"));
    says(m, "pwd", "/");
    Volume v(m.imageNow());
    CHECK(v.lookup("/d1/d2/f"));
}

TEST_CASE("cd into a mounted volume and out of it by ..; pwd across the mount; what is no directory refused") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), true);              /* the RAM disk at /tmp */
    atPrompt(m);
    REQUIRE(m.command("cd /tmp"));
    says(m, "pwd", "/tmp");
    REQUIRE(m.command("mkdir t"));
    REQUIRE(m.command("cd t"));
    says(m, "pwd", "/tmp/t");
    REQUIRE(m.command("cd ../.."));
    says(m, "pwd", "/");
    REQUIRE(m.command("cd /nothere"));
    CHECK(after(m, "cd /nothere").find("sh: /nothere: not found") != std::string::npos);
    REQUIRE(m.command("cd /etc/motd"));
    CHECK(after(m, "cd /etc/motd").find("sh: /etc/motd: not a directory") != std::string::npos);
    says(m, "pwd", "/");
}

/* A path a call takes is 64 bytes at most, its NUL too; directories go as
 * deep as one likes by relative paths, past what a path can name from the
 * root.  Nothing of that brings the system down: the kernel walks a path
 * without recursion, and the programs that build paths - du, ls -l, cp,
 * the shell's prompt and pwd - refuse one that does not fit instead of
 * cutting it, du's recursion with it. */
TEST_CASE("deep directories and long paths: refused, the system runs on") {
    if (!unixos::built()) { MESSAGE("unix.dsk not built - skipped"); return; }
    Machine m(unixos::readImage(), false);
    atPrompt(m);
    const std::string name = "abcdefghijklmn";              /* 14, a name's most */
    for (int i = 0; i < 5; ++i) {                            /* /home and 5 * 15 = 80 */
        REQUIRE(m.command("mkdir " + name, 3000));
        REQUIRE(m.command("cd " + name, 3000));
    }
    REQUIRE(m.command("echo deep > f", 3000));
    REQUIRE(m.command("cat f", 3000));
    CHECK(after(m, "cat f").find("\ndeep\n#") != std::string::npos);
    REQUIRE(m.command("pwd", 3000));
    CHECK(after(m, "pwd").find("pwd: .: too long") != std::string::npos);
    REQUIRE(m.command("ls -l", 3000));
    CHECK(after(m, "ls -l").find(" f\n") != std::string::npos);
    const std::string longPath = "/home/" + name + "/" + name + "/" + name + "/" + name + "/f";
    REQUIRE(m.command("cat " + longPath, 3000));
    CHECK(after(m, "cat " + longPath).find(": too lo") != std::string::npos);   /* the row ends there */
    REQUIRE(m.command("cd", 3000));
    REQUIRE(m.command("du /home", 6000));
    {
        INFO(m.screen());
        CHECK(after(m, "du /home").find(": too long") != std::string::npos);
    }
    const std::string deep = name + "/" + name + "/" + name + "/" + name;
    REQUIRE(m.command("cp /etc/motd " + deep, 3000));
    CHECK(after(m, "cp /etc/motd " + deep).find(": too long") != std::string::npos);
    INFO(m.screen());
    CHECK(m.screen().find("panic") == std::string::npos);
    says(m, "pwd", "/home");
    says(m, "echo alive", "alive");
}

}
