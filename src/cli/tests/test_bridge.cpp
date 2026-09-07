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

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

    /* text the host puts in itself - a command typed for the user - goes
     * straight to the guest, past the commander */
    cli::bridge::typeToGuest("RUN DZ0:DIR\r");
    CHECK(cli::bridge::pendingTaps() == 12);
    drain();
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

namespace {

/* What has been written to `f` so far, read back through the same handle -
 * fopen_s takes a file exclusively, so no second reader can open it. */
std::string writtenSoFar(FILE *f)
{
    std::fflush(f);
    const long end = std::ftell(f);
    std::rewind(f);
    std::string out(static_cast<size_t>(end < 0 ? 0 : end), '\0');
    const size_t n = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    out.resize(n);
    std::fseek(f, 0, SEEK_END);
    return out;
}

/* "ESC [ <row> ; <col> H" at `at` of `s`. */
bool cursorPos(const std::string &s, size_t at, int &row, int &col)
{
    if (s.compare(at, 2, "\x1B[") != 0) return false;
    size_t i = at + 2;
    const auto number = [&](int &out) {
        const size_t start = i;
        out = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) out = out * 10 + (s[i] - '0');
        return i > start;
    };
    if (!number(row)) return false;
    if (i >= s.size() || s[i] != ';') return false;
    ++i;
    if (!number(col)) return false;
    return i < s.size() && s[i] == 'H';
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
    fopen_s(&drawn, (dir / "bridge_boot_drawn.txt").string().c_str(), "w+b");
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

    /* text left standing at the prompt: the terminal's own cursor sits on
     * that row, right after what was typed, and no shape is ever set -
     * the parent terminal's own is what shows */
    feed("abc");
    runUntilQuiet(emu, mirror, host, 60, 2000);
    const std::string painted = writtenSoFar(drawn);
    CHECK(painted.find(" q") == std::string::npos);
    const size_t show = painted.rfind("\x1B[?25h");
    REQUIRE(show != std::string::npos);
    const size_t place = painted.rfind("\x1B[", show - 1);
    int cursorRow = 0, cursorCol = 0;
    REQUIRE(cursorPos(painted, place, cursorRow, cursorCol));
    /* the same frame's row that carries the prompt */
    const size_t frameStart = painted.rfind("\x1B[1;1H", show);
    REQUIRE(frameStart != std::string::npos);
    const std::string frame = painted.substr(frameStart, show - frameStart);
    const size_t prompt = frame.rfind(".abc");
    REQUIRE(prompt != std::string::npos);
    int promptRow = 0, promptCol = 0;
    REQUIRE(cursorPos(frame, frame.rfind("\x1B[", prompt), promptRow, promptCol));
    CHECK(cursorRow == promptRow);
    CHECK(cursorCol == 5);            /* ".abc" typed, the cursor past it */

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
}
