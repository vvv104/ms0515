/*
 * Cli.hpp — command-line argument parsing shared by ms0515 and
 * ms0515-cli.
 *
 * The schema matches ms0515.exe's existing flags so a one-line GUI
 * invocation is also a valid CLI invocation.  CLI-only fields (none
 * yet — every flag here is meaningful to both binaries) would be
 * collected separately by ms0515-cli's own arg pass.
 */
#ifndef MS0515_APP_CLI_HPP
#define MS0515_APP_CLI_HPP

#include <string>

namespace ms0515::app {

struct CliArgs {
    std::string romPath;
    /* One single-side image per core FDC unit (FD0..FD3 indexing).
     * Use ms0515::app::fdcUnitFor(drive, side) to compute the slot. */
    std::string fdPath[4];
    /* One double-sided image covering both sides of a drive.  Mutually
     * exclusive with fdPath[fdcUnitFor(drive, 0|1)] for the same
     * drive — App.cpp enforces and reports the conflict. */
    std::string dsPath[2];
    std::string screenshotPath;
    int         maxFrames = 0;          /* 0 = run forever */
    int         screenshotFrame = 0;
    int         historySize = -1;       /* -1 = take from config, 0 = off */
    int         historyWatchAddr = -1;
    int         historyWatchLen  = -1;
    int         historyReadWatchAddr = -1;
    int         historyReadWatchLen  = -1;
    /* Set by parseArgs when it walks argv and hits a token it
     * doesn't recognise (after retired-alias translation).  The GUI
     * uses this to bail out without opening a window — a typo in
     * the launcher command should fail fast, not boot the emulator
     * with a partly-applied config.  The CLI ignores the flag and
     * just leaves the stderr warning on the user's screen. */
    bool        unknownArgSeen = false;
};

/* Parse argv into CliArgs.  Unknown flags emit a warning to stderr but
 * do not abort.  Retired flags (--fd0..fd3, --disk, --drive) are
 * detected and translated to a friendly error message. */
CliArgs parseArgs(int argc, char **argv);

class Config;

/* Fold the persistent Config defaults into `cli` for any slot the
 * command line left empty: rom, fdPath[0..3], dsPath[0..1].  CLI args
 * always win.  Returns the merged CliArgs by value. */
CliArgs mergeCliOverConfig(CliArgs cli, const Config &cfg);

} /* namespace ms0515::app */

#endif /* MS0515_APP_CLI_HPP */
