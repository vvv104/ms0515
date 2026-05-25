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

#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>
#include <string_view>

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
  --emt-trace             Per-byte trace of .TTYOUT + EMT histogram
                          (stderr at exit).  Diagnostic only.
  -h, --help              Show this help and exit.

Persistent config: ms0515.yaml in the same directory as the binary;
written by ms0515.exe when settings change in the UI.  CLI args override
the config; absent CLI args fall back to the config; absent config
falls back to the default ROM next to the binary.

Quit hotkey (interactive session):  Ctrl-]
)";

/* Per-binary local flags that aren't part of the shared `CliArgs`
 * schema.  Currently just --help and --emt-trace; everything else
 * comes through libapp. */
struct LocalFlags {
    bool help     = false;
    bool emtTrace = false;
};

LocalFlags pickLocalFlags(int argc, char **argv)
{
    LocalFlags out;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help")  out.help     = true;
        if (a == "--emt-trace")          out.emtTrace = true;
    }
    return out;
}

}  /* namespace */

int main(int argc, char **argv)
{
    const LocalFlags local = pickLocalFlags(argc, argv);
    if (local.help) {
        std::fputs(kHelp.data(), stdout);
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
        std::println(stderr,
            "error: ROM not found.  Pass --rom <path> or put "
            "ms0515-roma.rom under assets/rom/ next to the binary.");
        return 1;
    }

    ms0515::cli::installInterruptHandler();
    ms0515::cli::setTerminalRawMode();
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);

    ms0515::Emulator emu;
    if (!emu.loadRomFile(rom)) {
        ms0515::cli::restoreTerminal();
        std::println(stderr, "error: failed to load ROM: {}", rom);
        return 1;
    }
    if (!app::mountDisksFromCli(emu, cli)) {
        ms0515::cli::restoreTerminal();
        return 1;
    }
    emu.reset();
    ms0515::cli::bridge::install(emu);
    ms0515::cli::bridge::setEmtTrace(local.emtTrace);

    /* VramMirror — hook-driven cell-by-cell mirror of the hires text
     * plane.  Per-frame flushFrame emits ANSI cursor-positioned UTF-8
     * for every changed cell to stdout. */
    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(stdout);
    std::fputs("\x1B[2J\x1B[H", stdout);

    /* Permit keystroke injection once VRAM has been quiet (no
     * substantial paint activity) for a while — that's our "kernel
     * sits at a prompt" signal.  See VramMirror::framesIdle. */
    constexpr int kInputReadyIdleFrames = 200;

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
    }

    ms0515::cli::restoreTerminal();
    ms0515::cli::bridge::dumpEmtCounts();

    if (emu.halted()) {
        std::println(stderr,
            "\nms0515-cli: CPU halted after {} frames (PC=0{:06o})",
            frame_count, emu.pc());
    } else if (ms0515::cli::shouldQuit()) {
        std::println(stderr,
            "\nms0515-cli: interrupted after {} frames (PC=0{:06o})",
            frame_count, emu.pc());
    } else if (cli.maxFrames > 0) {
        std::println(stderr,
            "\nms0515-cli: stopped after {} frames (--frames cap)",
            frame_count);
    }
    return 0;
}
