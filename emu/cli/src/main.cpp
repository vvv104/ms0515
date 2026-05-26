/*
 * main.cpp — ms0515-cli entry point.
 *
 * Same flag schema and same YAML config (`ms0515.yaml`) as the SDL
 * frontend; the two binaries are interchangeable for everything that
 * doesn't involve a graphical screen.  Mounts disks per CliArgs + Config
 * (CLI args win), boots the RT-11 kernel through emu/lib/, and presents
 * VRAM through ms0515::VramMirror on the host terminal.  Input flows
 * the other way: bridge.cpp drains stdin into the MS-7004 emulation.
 */

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>
#include <ms0515/app/Cli.hpp>
#include <ms0515/app/Config.hpp>
#include <ms0515/app/Disks.hpp>
#include <ms0515/app/Paths.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

#include "Platform.hpp"
#include "StdioBridge.hpp"

namespace app = ms0515::app;

namespace {

constexpr std::string_view kHelp = R"(usage: ms0515-cli [options]

Boot the MS-0515 emulator without a GUI and route its terminal in/out
through the host console.  Accepts the same flags as ms0515.exe and
reads the same ms0515.yaml settings file from the executable's
directory; either binary's last-used disks / ROM are picked up by the
other.

options:
  --rom <path>            ROM image (assets/rom/ms0515-romb.rom by
                          default — pass ms0515-roma.rom for Rodionov).
  --disk0 <path>          Mount double-sided image on drive 0.
                          (alias: -d0)
  --disk1 <path>          Mount double-sided image on drive 1.
                          (alias: -d1)
  --disk0-side0 <path>    Mount single-side image on drive 0 side 0.
                          (alias: -d0s0)
  --disk0-side1 <path>    Mount single-side image on drive 0 side 1.
                          (alias: -d0s1)
  --disk1-side0 <path>    Mount single-side image on drive 1 side 0.
                          (alias: -d1s0)
  --disk1-side1 <path>    Mount single-side image on drive 1 side 1.
                          (alias: -d1s1)
  --frames <N>            Stop after N emu frames (default: unlimited).
                          Useful for smoke-testing.
  --realtime              Throttle the emulator to the MS-0515's
                          original 50 Hz refresh, so OS-side timing
                          (sleep-driven UIs, timer-based code)
                          matches the hardware.  Without this flag
                          the loop runs as fast as the host CPU
                          allows.
  -h, --help              Show this help and exit.
  -V, --version           Print version and exit.

Persistent config: ms0515.yaml in the same directory as the binary;
written by ms0515.exe when settings change in the UI.  CLI args override
the config; absent CLI args fall back to the config; absent config
falls back to the default ROM next to the binary.

Quit hotkey (interactive session):  Ctrl-]
)";

/* Per-binary local flags that aren't part of the shared `CliArgs`
 * schema.  Everything else comes through libapp. */
struct CliLocalFlags {
    bool help     = false;
    bool version  = false;
    bool realtime = false;
};

CliLocalFlags pickCliLocalFlags(int argc, char **argv)
{
    CliLocalFlags out;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help")     out.help     = true;
        if (a == "-V" || a == "--version")  out.version  = true;
        if (a == "--realtime")              out.realtime = true;
    }
    return out;
}

}  /* namespace */

int main(int argc, char **argv)
{
    /* Switch the host console to UTF-8 early so the help text's
     * en/em dashes render instead of CP437 mojibake.
     * setTerminalRawMode() sets the same codepage later, but we
     * exit on --help / --version before reaching that. */
    ms0515::cli::enableUtf8Output();

    const CliLocalFlags local = pickCliLocalFlags(argc, argv);
    if (local.help) {
        std::fputs(kHelp.data(), stdout);
        return 0;
    }
    if (local.version) {
        std::fprintf(stdout, "ms0515-cli %s\n", MS0515_VERSION_STRING);
        return 0;
    }

    app::CliArgs cli  = app::parseArgs(argc, argv);
    app::Config  cfg  = app::Config::load();
    cli              = app::mergeCliOverConfig(std::move(cli), cfg);

    /* No disks?  Bail out — ms0515.exe would let the user pick from the
     * menu, but the CLI has no GUI fallback. */
    bool anyDisk = false;
    for (int i = 0; i < 4 && !anyDisk; ++i) anyDisk |= !cli.fdPath[i].empty();
    for (int i = 0; i < 2 && !anyDisk; ++i) anyDisk |= !cli.dsPath[i].empty();
    if (!anyDisk) {
        std::fputs(
            "error: no disk image given.  Pass --disk0 <path> or set\n"
            "       disk0: ... in ms0515.yaml next to the binary.\n\n",
            stderr);
        std::fputs(kHelp.data(), stderr);
        return 2;
    }

    const std::string rom = app::resolveRom(cli.romPath, cfg.romPath);
    if (rom.empty()) {
        std::fputs(
            "error: ROM not found.  Pass --rom <path> or put "
            "ms0515-roma.rom under assets/rom/ next to the binary.\n",
            stderr);
        return 1;
    }

    ms0515::cli::installInterruptHandler();
    ms0515::cli::setTerminalRawMode();
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);

    ms0515::Emulator emu;
    if (!emu.loadRomFile(rom)) {
        ms0515::cli::restoreTerminal();
        std::fprintf(stderr, "error: failed to load ROM: %s\n", rom.c_str());
        return 1;
    }
    if (!app::mountDisksFromCli(emu, cli)) {
        ms0515::cli::restoreTerminal();
        return 1;
    }
    emu.reset();
    ms0515::cli::bridge::install(emu);

    /* VramMirror — hook-driven cell-by-cell mirror of the hires text
     * plane.  Per-frame flushFrame emits ANSI cursor-positioned UTF-8
     * for every changed cell to stdout. */
    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(stdout);
    /* Clear screen, home cursor, and hide the host-terminal cursor.
     * The guest OS draws its own cursor as a blinking `_` glyph in
     * VRAM (which the mirror renders cell-by-cell like any other
     * char); leaving the host cursor visible on top of that gives
     * two cursors and the host one lags a frame behind the guest one
     * because we park it at the last cell we wrote, not the cell the
     * guest currently considers "next". */
    std::fputs("\x1B[2J\x1B[H\x1B[?25l", stdout);

    /* Permit keystroke injection once VRAM has been quiet (no
     * substantial paint activity) for a while — that's our "kernel
     * sits at a prompt" signal.  See VramMirror::framesIdle. */
    constexpr int kInputReadyIdleFrames = 200;

    /* --realtime pace: cap the loop at the MS-0515's 50 Hz refresh
     * so OS timing (cursor blink, sleep-driven UIs) matches the
     * hardware.  Without the flag the loop runs flat-out — useful
     * for compile / batch jobs where wall-clock fidelity doesn't
     * matter and the host CPU should crunch frames as fast as it
     * can.  Floppy operations are unaffected either way (the FDC
     * emulator never simulated real seek/read delays). */
    using Clock = std::chrono::steady_clock;
    constexpr auto kFramePeriod = std::chrono::microseconds(20'000);  /* 50 Hz */
    auto nextFrameAt = Clock::now();

    long frame_count = 0;
    while (!emu.halted() && !ms0515::cli::shouldQuit()) {
        if (cli.maxFrames > 0 && frame_count >= cli.maxFrames) break;
        ms0515::cli::bridge::pumpInput();
        (void)emu.stepFrame();
        mirror.flushFrame();
        if (mirror.framesIdle() >= kInputReadyIdleFrames) {
            ms0515::cli::bridge::setInputReady(true);
        }
        ++frame_count;

        if (local.realtime) {
            nextFrameAt += kFramePeriod;
            const auto now = Clock::now();
            if (now < nextFrameAt) {
                std::this_thread::sleep_until(nextFrameAt);
            } else {
                /* Fell behind by more than a frame — reset the
                 * schedule so we don't "catch up" by running flat-
                 * out, which would defeat the throttle. */
                nextFrameAt = now;
            }
        }
    }

    /* Show the host cursor again before handing the terminal back. */
    std::fputs("\x1B[?25h", stdout);
    std::fflush(stdout);
    ms0515::cli::restoreTerminal();

    if (emu.halted()) {
        std::fprintf(stderr,
            "\nms0515-cli: CPU halted after %ld frames (PC=0%06o)\n",
            frame_count, emu.pc());
    } else if (ms0515::cli::shouldQuit()) {
        std::fprintf(stderr,
            "\nms0515-cli: interrupted after %ld frames (PC=0%06o)\n",
            frame_count, emu.pc());
    } else if (cli.maxFrames > 0) {
        std::fprintf(stderr,
            "\nms0515-cli: stopped after %ld frames (--frames cap)\n",
            frame_count);
    }
    return 0;
}
