/*
 * main.cpp — MS0515 emulator frontend (SDL2 + Dear ImGui).
 *
 * Runs the core emulator in a 50/60/72 Hz frame loop and presents the
 * decoded framebuffer through an SDL_Renderer texture.  A Dear ImGui
 * debugger overlay provides register/disassembly/breakpoint views and
 * run/step controls.
 *
 * CLI:
 *   ms0515 [--rom <path>]
 *          [--disk0 <path>] | [--disk0-side0 <path>] [--disk0-side1 <path>]
 *          [--disk1 <path>] | [--disk1-side0 <path>] [--disk1-side1 <path>]
 *          (short aliases: -d0/-d0s0/-d0s1, -d1/-d1s0/-d1s1)
 *          [--history-size N]               (events; 0 disables)
 *          [--history-watch-addr A] [--history-watch-len L]  (MEMW evts)
 *          [--history-read-watch-addr A] [--history-read-watch-len L]
 *
 * Disk-mount options come in two flavours:
 *   - Single-side mounts: `--diskN-sideM` (-dNsM) — one 409600-byte
 *     image per physical side.  The core driver calls these logical
 *     units FD0..FD3, mapped via bits 1:0 of System Register A:
 *
 *         --disk0-side0  ↔  drive 0, lower head (= core FD0)
 *         --disk0-side1  ↔  drive 0, upper head (= core FD2)
 *         --disk1-side0  ↔  drive 1, lower head (= core FD1)
 *         --disk1-side1  ↔  drive 1, upper head (= core FD3)
 *
 *   - Double-sided mount: `--diskN` (-dN) — one 819200-byte image
 *     in track-interleaved layout (T0S0, T0S1, T1S0, T1S1, ...).
 *     `--diskN` and `--diskN-sideM` for the same N are mutually
 *     exclusive.
 *
 * Defaults: looks for assets/rom/ms0515-roma.rom (the patched ROM-A,
 * relative to either the executable directory or the current working
 * directory) when --rom is not given.
 *
 * Everything beyond argv-parsing lives in App; main() is just the
 * entry point. */

#include "App.hpp"
#include "Config.hpp"   /* parseArgs lives here now */
#include "Platform.hpp" /* attachConsoleForOutput */

#include <cstdio>
#include <print>
#include <string_view>

namespace {

constexpr std::string_view kHelp = R"(usage: ms0515 [options]

SDL2 + Dear ImGui frontend for the MS-0515 emulator.  Same disk /
ROM flag schema as ms0515-cli; either binary's last-used mounts
are picked up by the other via the shared ms0515.yaml settings file.

options:
  --rom <path>            ROM image (defaults to whatever's in
                          ms0515.yaml, falling back to
                          assets/rom/ms0515-roma.rom under the
                          binary's directory).
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
  --screenshot <path>     Save a PNG of the framebuffer at exit and
                          terminate; pairs naturally with --frames.
  --screenshot-frame <N>  Take the screenshot at exactly frame N.
  --history-size <N>      CPU trace ring buffer length (0 = off).
  --history-watch-addr <A> [--history-watch-len <L>]
                          Record a write-watch trace ring for the
                          memory range starting at A.
  --history-read-watch-addr <A> [--history-read-watch-len <L>]
                          Same idea for read-watches.
  -h, --help              Show this help and exit.
  -V, --version           Print version and exit.
)";

bool wantsHelp(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") return true;
    }
    return false;
}

bool wantsVersion(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-V" || a == "--version") return true;
    }
    return false;
}

}  /* namespace */

int main(int argc, char **argv)
{
    if (argc > 1) ms0515_frontend::attachConsoleForOutput();

    if (wantsHelp(argc, argv)) {
        std::fputs(kHelp.data(), stdout);
        std::fflush(stdout);
        return 0;
    }
    if (wantsVersion(argc, argv)) {
        std::println("ms0515 {}", MS0515_VERSION_STRING);
        std::fflush(stdout);
        return 0;
    }
    ms0515_frontend::App app(ms0515_frontend::parseArgs(argc, argv));
    return app.run();
}
