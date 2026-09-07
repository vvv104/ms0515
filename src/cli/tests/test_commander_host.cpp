/*
 * test_commander_host.cpp — the commander over a machine, driven by keys:
 * Ctrl+\ brings it up, typed text is the guest's and reaches its keyboard,
 * the panel keys are the commander's, F10 asks, Ctrl+O hides the panels.
 */
#include "CommanderHost.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/app/Cli.hpp>

#include <doctest/doctest.h>

#include <filesystem>

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
