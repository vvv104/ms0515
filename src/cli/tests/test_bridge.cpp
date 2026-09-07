/*
 * test_bridge.cpp — the host's bytes through the bridge into the guest's
 * keyboard, with the commander's sink in between: what is typed while
 * the panels are up must still reach the machine.
 */
#include "CommanderHost.hpp"
#include "StdioBridge.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/app/Cli.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace ms0515;

namespace {

void feed(const std::string &s)
{
    cli::bridge::feedHostBytes(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

/* pump until the taps are out, or give up */
void drain()
{
    for (int i = 0; i < 200 && cli::bridge::pendingTaps() > 0; ++i) cli::bridge::pumpInput();
}

} // namespace

TEST_CASE("typed bytes become taps for the guest whether the commander is down or up; the panel keys do not")
{
    const fs::path dir = fs::path(TESTS_BUILD_DIR) / "scratch";
    fs::create_directories(dir);
    const fs::path disk = dir / "bridge_osa.dsk";
    fs::copy_file(fs::path(FIXTURE_DISKS_DIR) / "test_osa.dsk", disk, fs::copy_options::overwrite_existing);
    Emulator emu;
    REQUIRE(emu.mountDisk(0, disk.string()));
    VramMirror mirror;
    mirror.attach(emu);
    app::CliArgs cliArgs;
    cliArgs.fdPath[0] = disk.string();
    cli::CommanderHost host(emu, mirror, cliArgs, nullptr);

    cli::bridge::install(emu);
    cli::bridge::setInputReady(true);
    cli::bridge::setHostKeySink([&host](const files::HostKey &k) { return host.onKey(k); });

    /* the commander down: three letters and Enter are four taps */
    feed("dir\r");
    CHECK(cli::bridge::pendingTaps() == 4);
    drain();
    CHECK(cli::bridge::pendingTaps() == 0);

    /* up: the same */
    feed("\x1C");
    CHECK(host.active());
    CHECK(cli::bridge::pendingTaps() == 0);
    feed("dir\r");
    CHECK(cli::bridge::pendingTaps() == 4);
    drain();
    CHECK(cli::bridge::pendingTaps() == 0);

    /* the panel keys are the commander's: nothing for the guest */
    feed("\x1B[B");
    feed("\x09");
    CHECK(cli::bridge::pendingTaps() == 0);

    cli::bridge::setHostKeySink(nullptr);
    host.shutdown(false);
}

namespace {

/* Frames until the machine's screen has been quiet for `quiet` of them. */
void runUntilQuiet(Emulator &emu, VramMirror &mirror, cli::CommanderHost &host, int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap && q < quiet; ++i) {
        cli::bridge::pumpInput();
        (void)emu.stepFrame();
        mirror.flushFrame();
        host.frame();
        q = mirror.framesIdle() == 0 ? 0 : q + 1;
    }
}

bool screenHas(const VramMirror &mirror, const std::string &needle)
{
    const auto snap = mirror.snapshot();
    for (int r = 0; r < VramMirror::kRows; ++r)
        if (snap.row(r).find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST_CASE("the booted machine takes a DIR typed with the commander down, and one typed with it up")
{
    const fs::path dir = fs::path(TESTS_BUILD_DIR) / "scratch";
    fs::create_directories(dir);
    const fs::path disk = dir / "bridge_boot_osa.dsk";
    fs::copy_file(fs::path(FIXTURE_DISKS_DIR) / "test_osa.dsk", disk, fs::copy_options::overwrite_existing);
    Emulator emu;
    REQUIRE(emu.loadRomFile(ASSETS_DIR "/rom/ms0515-roma.rom"));
    REQUIRE(emu.mountDisk(0, disk.string()));
    emu.reset();
    VramMirror mirror;
    mirror.attach(emu);
    app::CliArgs cliArgs;
    cliArgs.fdPath[0] = disk.string();
    /* a real output stream, so the host draws as it does on a terminal */
    FILE *drawn = nullptr;
    fopen_s(&drawn, (dir / "bridge_boot_drawn.txt").string().c_str(), "wb");
    REQUIRE(drawn != nullptr);
    cli::CommanderHost host(emu, mirror, cliArgs, drawn);
    cli::bridge::install(emu);
    cli::bridge::setHostKeySink([&host](const files::HostKey &k) { return host.onKey(k); });

    runUntilQuiet(emu, mirror, host, 200, 4000);   /* the boot, to the prompt */
    cli::bridge::setInputReady(true);
    REQUIRE(screenHas(mirror, "."));

    /* down: DIR lists the volume */
    feed("dir\r");
    runUntilQuiet(emu, mirror, host, 150, 4000);
    CHECK(screenHas(mirror, "SAV"));
    CHECK(screenHas(mirror, ".dir"));

    /* up: the same DIR must reach the machine and be echoed */
    feed("\x1C");
    REQUIRE(host.active());
    feed("dir\r");
    runUntilQuiet(emu, mirror, host, 150, 4000);
    CHECK(screenHas(mirror, ".dir"));
    CHECK(screenHas(mirror, "SYS"));

    /* F10 + Yes: the panels down, and the terminal repainted in full - the
     * mirror's history (every cell emitted) grows by the whole screen */
    const size_t before = mirror.history().size();
    feed("\x1B[21~");
    feed("y");
    CHECK_FALSE(host.active());
    runUntilQuiet(emu, mirror, host, 3, 10);
    CHECK(mirror.history().size() - before >= 1000);

    cli::bridge::setHostKeySink(nullptr);
    host.shutdown(false);
    std::fclose(drawn);
    /* what the host drew while the panels were up carried the typed text */
    FILE *back = nullptr;
    fopen_s(&back, (dir / "bridge_boot_drawn.txt").string().c_str(), "rb");
    REQUIRE(back != nullptr);
    std::string picture;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, back)) > 0;) picture.append(buf, n);
    std::fclose(back);
    CHECK(picture.find(".dir") != std::string::npos);
}
