#define _CRT_SECURE_NO_WARNINGS
#include "Cli.hpp"
#include "Config.hpp"   /* fdcUnitFor, parseNumber */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace ms0515_frontend {

namespace {

/* Single-side disk-mount option table.  Each entry binds a long form
 * (`--diskN-sideM`) and a short alias (`-dNsM`) to a (drive, side)
 * pair. */
struct DiskOption {
    const char *longForm;
    const char *shortForm;
    int         drive;
    int         side;
};
constexpr DiskOption kDiskOptions[] = {
    {"--disk0-side0", "-d0s0", 0, 0},
    {"--disk0-side1", "-d0s1", 0, 1},
    {"--disk1-side0", "-d1s0", 1, 0},
    {"--disk1-side1", "-d1s1", 1, 1},
};

/* Double-sided disk-mount option table.  Each entry binds a long
 * form (`--diskN`) and a short alias (`-dN`) to a drive index. */
struct DoubleSidedOption {
    const char *longForm;
    const char *shortForm;
    int         drive;
};
constexpr DoubleSidedOption kDoubleSidedOptions[] = {
    {"--disk0", "-d0", 0},
    {"--disk1", "-d1", 1},
};

/* Detects the legacy CLI options we removed (--fd0..fd3, --disk,
 * --drive) and emits a friendly migration message naming the
 * replacement.  Returns `true` if `arg` was a legacy option (the
 * caller should also skip its accompanying value, if any). */
bool reportRetiredArg(const std::string &arg)
{
    if (arg == "--fd0" || arg == "--fd1" ||
        arg == "--fd2" || arg == "--fd3") {
        std::fprintf(stderr,
            "error: '%s' was removed.  Use the disk/side names instead:\n"
            "  --fd0 → --disk0-side0   (-d0s0)\n"
            "  --fd1 → --disk1-side0   (-d1s0)\n"
            "  --fd2 → --disk0-side1   (-d0s1)\n"
            "  --fd3 → --disk1-side1   (-d1s1)\n",
            arg.c_str());
        return true;
    }
    if (arg == "--disk" || arg == "--drive") {
        std::fprintf(stderr,
            "error: '%s' was removed.  Use --diskN-sideM (or -dNsM) "
            "to mount one side of a drive.\n",
            arg.c_str());
        return true;
    }
    return false;
}

} /* anonymous namespace */

CliArgs parseArgs(int argc, char **argv)
{
    CliArgs out;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--rom" && i + 1 < argc) {
            out.romPath = argv[++i];
        } else if (reportRetiredArg(a)) {
            if (i + 1 < argc) ++i;
        } else if (auto *opt = std::find_if(
                       std::begin(kDiskOptions), std::end(kDiskOptions),
                       [&](const DiskOption &o) {
                           return a == o.longForm || a == o.shortForm;
                       });
                   opt != std::end(kDiskOptions) && i + 1 < argc) {
            out.fdPath[fdcUnitFor(opt->drive, opt->side)] = argv[++i];
        } else if (auto *dsOpt = std::find_if(
                       std::begin(kDoubleSidedOptions),
                       std::end(kDoubleSidedOptions),
                       [&](const DoubleSidedOption &o) {
                           return a == o.longForm || a == o.shortForm;
                       });
                   dsOpt != std::end(kDoubleSidedOptions) && i + 1 < argc) {
            out.dsPath[dsOpt->drive] = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            out.maxFrames = std::atoi(argv[++i]);
        } else if (a == "--screenshot" && i + 1 < argc) {
            out.screenshotPath = argv[++i];
        } else if (a == "--screenshot-frame" && i + 1 < argc) {
            out.screenshotFrame = std::atoi(argv[++i]);
        } else if (a == "--history-size" && i + 1 < argc) {
            out.historySize = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--history-watch-addr" && i + 1 < argc) {
            out.historyWatchAddr = Paths::parseNumber(argv[++i]);
        } else if (a == "--history-watch-len" && i + 1 < argc) {
            out.historyWatchLen  = Paths::parseNumber(argv[++i]);
        } else if (a == "--history-read-watch-addr" && i + 1 < argc) {
            out.historyReadWatchAddr = Paths::parseNumber(argv[++i]);
        } else if (a == "--history-read-watch-len" && i + 1 < argc) {
            out.historyReadWatchLen  = Paths::parseNumber(argv[++i]);
        } else {
            std::fprintf(stderr, "warning: unknown argument '%s'\n", a.c_str());
        }
    }
    /* If a screenshot frame is set but no explicit --frames, auto-stop
     * after the screenshot so headless runs don't hang. */
    if (out.screenshotFrame > 0 && out.maxFrames <= 0) {
        out.maxFrames = out.screenshotFrame;
    }
    return out;
}

} /* namespace ms0515_frontend */
