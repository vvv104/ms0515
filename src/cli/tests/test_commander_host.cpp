/*
 * test_commander_host.cpp — the commander over a machine, driven by keys:
 * Ctrl+\ brings it up, typed text is the guest's and reaches its keyboard,
 * the panel keys are the commander's, F10 asks, Ctrl+O hides the panels.
 */
#include "CommanderHost.hpp"

#include <ftxui/screen/terminal.hpp>

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/app/Cli.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace ms0515;

namespace {

struct Machine {
    fs::path disk;
    Emulator emu;
    VramMirror mirror;
    app::CliArgs cli;
    Machine()
    {
        const fs::path dir = fs::path(TESTS_BUILD_DIR) / "scratch";
        fs::create_directories(dir);
        disk = dir / "host_osa.dsk";
        fs::copy_file(fs::path(FIXTURE_DISKS_DIR) / "test_osa.dsk", disk, fs::copy_options::overwrite_existing);
        cli.fdPath[0] = disk.string();
        REQUIRE(emu.mountDisk(0, disk.string()));
        mirror.attach(emu);
    }
};

files::HostKey byte(uint8_t b) { return files::HostKey::ofByte(b); }

} // namespace

TEST_CASE("the host never sets a cursor shape - the terminal keeps the one it is configured with - and hides the cursor while it has nowhere to put it")
{
    Machine m;
    std::string painted;
    {
        cli::CommanderHost host(m.emu, m.mirror, m.cli, [&painted](std::string_view s) { painted.append(s); });
        CHECK(host.onKey(byte(0x1C)));
        host.shutdown(false);
    }
    /* DECSCUSR - "ESC [ <n> SP q" - would override the terminal's own shape */
    CHECK(painted.find(" q") == std::string::npos);
    /* the machine has not drawn a cursor yet: none is shown */
    CHECK(painted.find("\x1B[?25l") != std::string::npos);
}

TEST_CASE("typed text goes to the guest while the panels are up, the panel keys stay with the commander")
{
    Machine m;
    cli::CommanderHost host(m.emu, m.mirror, m.cli, nullptr);
    CHECK_FALSE(host.active());
    CHECK_FALSE(host.onKey(byte('d')));                /* the commander is down: the guest's */
    CHECK(host.onKey(byte(0x1C)));                     /* Ctrl+\ */
    CHECK(host.active());
    CHECK_FALSE(host.onKey(byte('d')));                /* typed: the guest's, even with the panels up */
    CHECK_FALSE(host.onKey(byte('i')));
    CHECK_FALSE(host.onKey(byte('r')));
    CHECK_FALSE(host.onKey(byte(0x0D)));               /* Enter after typing: the guest's */
    CHECK(host.onKey(byte(0x0D)));                     /* Enter with nothing typed: the panel's - it views the entry */
    CHECK(host.onKey(byte(0x1B)));                     /* Esc closes the viewer */
    CHECK(host.onKey(files::HostKey::ofSpecial(files::SpecialKey::down)));
    CHECK(host.onKey(byte(0x1C)));                     /* Ctrl+\ again: swallowed, still up */
    CHECK(host.active());
    /* Ctrl+O: the panels hidden, the typed text still the guest's, F3 too */
    CHECK(host.onKey(byte(0x0F)));
    CHECK_FALSE(host.onKey(byte('x')));
    CHECK_FALSE(host.onKey(files::HostKey::ofSpecial(files::SpecialKey::f3)));
    CHECK(host.onKey(files::HostKey::ofSpecial(files::SpecialKey::pageUp)));    /* the scrollback, the host's */
    CHECK(host.onKey(files::HostKey::ofSpecial(files::SpecialKey::pageDown)));
    CHECK(host.onKey(byte(0x0F)));
    /* F10 asks; 'y' answers and the panels come down */
    CHECK(host.onKey(files::HostKey::ofSpecial(files::SpecialKey::f10)));
    CHECK(host.active());
    CHECK(host.onKey(byte('y')));
    CHECK_FALSE(host.active());
    host.shutdown(false);
}

TEST_CASE("the hint at the bottom of the terminal names the keys, saves and restores the cursor, and keeps off the machine's rows")
{
    const std::string hint = cli::hintLine(80, 30);
    CHECK(hint.find("Ctrl+\\") != std::string::npos);
    CHECK(hint.find("Ctrl+]") != std::string::npos);
    /* the bottom row, and the guest's cursor put back where it was */
    CHECK(hint.find("\x1B[30;1H") != std::string::npos);
    CHECK(hint.rfind("\x1B" "7", 0) == 0);
    CHECK(hint.size() >= 4);
    CHECK(hint.compare(hint.size() - 2, 2, "\x1B" "8") == 0);
    /* it never reaches the last column - some terminals scroll on that */
    const size_t textAt = hint.find("\x1B[7m") + 4;
    const size_t textEnd = hint.find("\x1B[27m");
    REQUIRE(textEnd != std::string::npos);
    CHECK(textEnd - textAt == 79);
    /* a terminal with no room below the machine's 25 rows gets none */
    CHECK(cli::hintLine(80, 25).empty());
    CHECK(cli::hintLine(80, 24).empty());
    CHECK(cli::hintLine(20, 40).empty());
}

TEST_CASE("the host writes the hint while the panels are down, and never while they are up")
{
    Machine m;
    std::string painted;
    const bool room = ftxui::Terminal::Size().dimy > ms0515::VramMirror::kRows;
    {
        cli::CommanderHost host(m.emu, m.mirror, m.cli, [&painted](std::string_view s) { painted.append(s); });
        host.frame();
        host.frame();
        const std::string beforeUp = painted;
        if (room) CHECK(beforeUp.find("Ctrl+]") != std::string::npos);
        const size_t hints = [&beforeUp] {
            size_t n = 0;
            for (size_t at = beforeUp.find("Ctrl+]"); at != std::string::npos; at = beforeUp.find("Ctrl+]", at + 1)) ++n;
            return n;
        }();
        CHECK(hints <= 1);                    /* drawn once, not every frame */

        CHECK(host.onKey(byte(0x1C)));        /* the panels up: the hint is not part of them */
        host.frame();
        host.frame();
        const std::string all = painted;
        size_t afterUp = 0;
        for (size_t at = all.find("Ctrl+]"); at != std::string::npos; at = all.find("Ctrl+]", at + 1)) ++afterUp;
        CHECK(afterUp == hints);
        host.shutdown(false);
    }
}
