/*
 * main.cpp — ms0515-cli entry point.
 *
 * Boots the real Omega RT-11 kernel via emu/lib/ and runs frames
 * until the kernel halts or the user hits Ctrl-C.  No console I/O
 * is wired yet — Stage 2 only verifies that the kernel reaches a
 * stable run state without any frontend driver.  Stage 3 plugs the
 * stdio bridge into cpu->trap_thunk so .TTYIN/.TTYOUT/.PRINT pass
 * through host stdin/stdout.
 */

#include <ms0515/Emulator.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

#include "Platform.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kHelp = R"(usage: ms0515-cli [options] <disk.dsk>

Boot Omega RT-11 from the given disk image and present it as a
console session.

options:
  --rom <path>   ROM image to load (default: ms0515-romb.rom shipped
                 with the project)
  --frames <N>   Stop after N frames have been stepped (default:
                 run until halt / Ctrl-C).  Useful for smoke-testing.
  -h, --help     Show this help and exit.
)";

struct Args {
    std::string rom    = MS0515_DEFAULT_ROM;
    std::string disk;
    long        frames = -1;       /* -1 = unlimited */
    bool        help   = false;
};

bool parseArgs(int argc, char *argv[], Args &out)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") {
            out.help = true;
            return true;
        }
        if (a == "--rom") {
            if (++i >= argc) {
                std::fprintf(stderr, "--rom requires a path argument\n");
                return false;
            }
            out.rom = argv[i];
            continue;
        }
        if (a == "--frames") {
            if (++i >= argc) {
                std::fprintf(stderr, "--frames requires a number\n");
                return false;
            }
            out.frames = std::strtol(argv[i], nullptr, 10);
            continue;
        }
        if (a.size() > 0 && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
        if (!out.disk.empty()) {
            std::fprintf(stderr,
                "multiple disk arguments: %s already given, "
                "extra: %s\n",
                out.disk.c_str(), argv[i]);
            return false;
        }
        out.disk = argv[i];
    }
    return true;
}

}  /* namespace */

int main(int argc, char *argv[])
{
    Args args;
    if (!parseArgs(argc, argv, args)) {
        std::fputs(kHelp.data(), stderr);
        return 2;
    }
    if (args.help) {
        std::fputs(kHelp.data(), stdout);
        return 0;
    }
    if (args.disk.empty()) {
        std::fputs("error: disk image path required\n", stderr);
        std::fputs(kHelp.data(), stderr);
        return 2;
    }
    if (!fs::exists(args.rom)) {
        std::println(stderr, "error: ROM not found: {}", args.rom);
        return 1;
    }
    if (!fs::exists(args.disk)) {
        std::println(stderr, "error: disk image not found: {}", args.disk);
        return 1;
    }

    ms0515::cli::installInterruptHandler();

    ms0515::Emulator emu;
    if (!emu.loadRomFile(args.rom)) {
        std::println(stderr, "error: failed to load ROM: {}", args.rom);
        return 1;
    }
    if (!emu.mountDisk(0, args.disk)) {
        std::println(stderr, "error: failed to mount disk: {}", args.disk);
        return 1;
    }
    emu.reset();

    std::println(stderr,
        "ms0515-cli: booting ROM={} disk={}",
        args.rom, args.disk);

    long frame_count = 0;
    while (!emu.halted() && !ms0515::cli::shouldQuit()) {
        if (args.frames >= 0 && frame_count >= args.frames) {
            std::println(stderr,
                "ms0515-cli: stopped after {} frames (--frames cap)",
                frame_count);
            break;
        }
        (void)emu.stepFrame();
        ++frame_count;
    }

    if (emu.halted()) {
        std::println(stderr,
            "ms0515-cli: CPU halted after {} frames (PC=0{:06o})",
            frame_count, emu.pc());
    } else if (ms0515::cli::shouldQuit()) {
        std::println(stderr,
            "ms0515-cli: interrupted after {} frames (PC=0{:06o})",
            frame_count, emu.pc());
    }
    return 0;
}
