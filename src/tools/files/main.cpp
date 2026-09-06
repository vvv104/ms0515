/*
 * main.cpp — ms0515-files: the machine's disks in two panels, in the
 * terminal.
 *
 * Takes the emulator's own disk flags (--disk0, --disk0-side1, --disk1,
 * --hd ...) through libapp's parser and starts on the disks ms0515.yaml
 * remembers, so the panels show what the emulator has mounted; a mount
 * made here is saved back for the emulator.  --no-config ignores the
 * yaml (flags only).
 */
#include "Mounts.hpp"
#include "Commander.hpp"

#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"
#include "Platform.hpp"

#include <cstdio>
#include <cstring>

namespace {

void usage()
{
    std::puts(
        "usage: ms0515-files [options]\n"
        "\n"
        "Two panels over the machine's disks - DZ0: DZ2: (drive A), DZ1: DZ3:\n"
        "(drive B), HD0:, or a DV0:/MZ0: whole-diskette volume - with the web\n"
        "commander's keys: F1 a file from the host, F2 files to the host, F3\n"
        "view, F5 copy, F6 rename / move, F7 squeeze, F8 delete, F9 init, F10\n"
        "quit; Tab the other panel, Insert marks, Enter views.  Alt+F1 / Alt+F2\n"
        "(or F4) choose the left / right panel's disk: a mounted device, or\n"
        "another image picked from a host listing shown in that panel.\n"
        "\n"
        "options (the emulator's, from libapp):\n"
        "  --disk0 <path> / --disk1 <path>        a two-sided image (or a DV/MZ volume) in drive A / B\n"
        "  --disk0-side0 <path> ... --disk1-side1  a single-sided image per side\n"
        "  --hd <path>                            the paravirtual hard disk image\n"
        "  --no-config                            ignore ms0515.yaml (flags only)\n"
        "  -h, --help\n"
        "\n"
        "Without flags the disks are those ms0515.yaml next to the binary\n"
        "remembers - what the emulator had mounted last.\n");
}

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) { usage(); return 0; }

    ms0515::cli::enableUtf8Output();
    const ms0515::app::CliArgs cli = ms0515::app::parseArgs(argc, argv);
    if (cli.unknownArgSeen) { usage(); return 2; }
    ms0515::app::Config config = cli.noConfig ? ms0515::app::Config{} : ms0515::app::Config::load();

    ms0515::files::Mounts mounts = ms0515::files::Mounts::fromEmulator(cli, config);
    const int rc = ms0515::files::runCommander(std::move(mounts), config, "Leave ms0515-files?");
    if (!cli.noConfig) config.save();
    return rc;
}
