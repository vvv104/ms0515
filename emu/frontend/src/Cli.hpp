/*
 * Cli.hpp — command-line argument parsing.
 *
 * Decoded shape of `argv` for use by main().  See the long banner in
 * main.cpp for the user-facing CLI synopsis.
 */
#pragma once

#include <string>

namespace ms0515_frontend {

struct CliArgs {
    std::string romPath;
    /* fdPath[unit] — one single-side image per core FDC unit
     * (FD0..FD3 indexing).  Frontend names these by (drive, side) —
     * see fdcUnitFor() in Config.hpp. */
    std::string fdPath[4];
    /* dsPath[drive] — one double-sided image covering both sides of a
     * drive.  Mutually exclusive with fdPath[fdcUnitFor(drive, 0|1)]. */
    std::string dsPath[2];
    std::string screenDumpPath;     /* --screen-dump: VRAM text output */
    std::string screenshotPath;
    int         maxFrames = 0;      /* 0 = run forever */
    int         screenshotFrame = 0;
    int         historySize = -1;   /* -1 = take from config, 0 = disabled */
    int         historyWatchAddr = -1;
    int         historyWatchLen  = -1;
    int         historyReadWatchAddr = -1;
    int         historyReadWatchLen  = -1;
};

/* Parse argv into CliArgs.  Unknown flags emit a warning to stderr but
 * do not abort.  Retired flags (--fd0..fd3, --disk, --drive) are
 * detected and translated to a friendly error message. */
CliArgs parseArgs(int argc, char **argv);

} /* namespace ms0515_frontend */
