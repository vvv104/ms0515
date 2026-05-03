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
#include "Cli.hpp"

int main(int argc, char **argv)
{
    ms0515_frontend::App app(ms0515_frontend::parseArgs(argc, argv));
    return app.run();
}
