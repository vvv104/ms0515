/*
 * test_cli.cpp — contract tests for ms0515::app::parseArgs and
 * mergeCliOverConfig.  Built from the header docs only.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Cli.hpp>
#include <ms0515/app/Config.hpp>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace app = ms0515::app;

namespace {

/* Drive a parseArgs() call from a list of string args without manually
 * juggling char* lifetime.  Returns the parsed CliArgs. */
app::CliArgs parse(std::initializer_list<const char *> tail)
{
    /* argv[0] is the program name by convention — parseArgs starts at
     * argv[1], so any non-empty placeholder works. */
    std::vector<const char *> argv;
    argv.push_back("ms0515-cli");
    for (auto a : tail) argv.push_back(a);
    return app::parseArgs(static_cast<int>(argv.size()),
                          const_cast<char **>(argv.data()));
}

}  // namespace

TEST_SUITE("parseArgs — defaults") {

TEST_CASE("no arguments → all paths empty, frame counters zero") {
    app::CliArgs a = parse({});
    CHECK(a.romPath.empty());
    CHECK(a.screenshotPath.empty());
    for (int i = 0; i < 4; ++i) CHECK(a.fdPath[i].empty());
    for (int i = 0; i < 2; ++i) CHECK(a.dsPath[i].empty());
    CHECK(a.hdPath.empty());
    CHECK(a.maxFrames == 0);
    CHECK(a.screenshotFrame == 0);
    /* historySize uses -1 as "take from config", not zero — the header
     * documents the sentinel. */
    CHECK(a.historySize          == -1);
    CHECK(a.historyWatchAddr     == -1);
    CHECK(a.historyWatchLen      == -1);
    CHECK(a.historyReadWatchAddr == -1);
    CHECK(a.historyReadWatchLen  == -1);
}

}  // TEST_SUITE


TEST_SUITE("parseArgs — --rom") {

TEST_CASE("--rom <path> populates romPath") {
    app::CliArgs a = parse({"--rom", "/path/to/rom.rom"});
    CHECK(a.romPath == "/path/to/rom.rom");
}

}  // TEST_SUITE


TEST_SUITE("parseArgs — disk options") {

TEST_CASE("--disk0 / -d0 mount double-sided on drive 0") {
    CHECK(parse({"--disk0", "ds.dsk"}).dsPath[0] == "ds.dsk");
    CHECK(parse({"-d0",     "ds.dsk"}).dsPath[0] == "ds.dsk");
}

TEST_CASE("--disk1 / -d1 mount double-sided on drive 1") {
    CHECK(parse({"--disk1", "ds.dsk"}).dsPath[1] == "ds.dsk");
    CHECK(parse({"-d1",     "ds.dsk"}).dsPath[1] == "ds.dsk");
}

TEST_CASE("side-specific options land in the right fdc unit") {
    {   app::CliArgs a = parse({"--disk0-side0", "s.dsk"});
        CHECK(a.fdPath[app::fdcUnitFor(0, 0)] == "s.dsk"); }
    {   app::CliArgs a = parse({"-d0s1", "s.dsk"});
        CHECK(a.fdPath[app::fdcUnitFor(0, 1)] == "s.dsk"); }
    {   app::CliArgs a = parse({"--disk1-side0", "s.dsk"});
        CHECK(a.fdPath[app::fdcUnitFor(1, 0)] == "s.dsk"); }
    {   app::CliArgs a = parse({"-d1s1", "s.dsk"});
        CHECK(a.fdPath[app::fdcUnitFor(1, 1)] == "s.dsk"); }
}

TEST_CASE("multiple disks can be given in one invocation") {
    app::CliArgs a = parse({"-d0", "ds.dsk",
                            "-d1s0", "ss.dsk"});
    CHECK(a.dsPath[0] == "ds.dsk");
    CHECK(a.fdPath[app::fdcUnitFor(1, 0)] == "ss.dsk");
}

TEST_CASE("--hd <path> populates hdPath") {
    CHECK(parse({"--hd", "winchester.hd"}).hdPath == "winchester.hd");
}

}  // TEST_SUITE


TEST_SUITE("parseArgs — frames + screenshot") {

TEST_CASE("--frames <N> sets maxFrames") {
    CHECK(parse({"--frames", "600"}).maxFrames == 600);
}

TEST_CASE("--screenshot <path> + --screenshot-frame <N> populate both") {
    app::CliArgs a = parse({"--screenshot", "shot.png",
                            "--screenshot-frame", "300"});
    CHECK(a.screenshotPath  == "shot.png");
    CHECK(a.screenshotFrame == 300);
    /* Header note: when screenshotFrame is set without --frames, the
     * parser auto-caps maxFrames at the screenshot frame so headless
     * runs don't hang.  Test the auto-cap separately so order-
     * sensitivity is explicit. */
    CHECK(a.maxFrames == 300);
}

TEST_CASE("--frames wins when both --frames and --screenshot-frame are given") {
    app::CliArgs a = parse({"--frames", "1000",
                            "--screenshot-frame", "300"});
    /* The implementation only auto-caps when maxFrames was zero.  An
     * explicit --frames should not be overridden by --screenshot-frame
     * — that would surprise the user. */
    CHECK(a.maxFrames       == 1000);
    CHECK(a.screenshotFrame == 300);
}

}  // TEST_SUITE


TEST_SUITE("parseArgs — history watchpoints") {

TEST_CASE("--history-size accepts decimal") {
    CHECK(parse({"--history-size", "256"}).historySize == 256);
}

TEST_CASE("watch-addr accepts hex (0x) and octal (0o) prefixes") {
    CHECK(parse({"--history-watch-addr", "0o177400"}).historyWatchAddr == 0177400);
    CHECK(parse({"--history-watch-addr", "0x1000"}).historyWatchAddr   == 0x1000);
}

TEST_CASE("read-watch fields land in their own slots") {
    app::CliArgs a = parse({"--history-read-watch-addr", "0o100",
                            "--history-read-watch-len",  "8"});
    CHECK(a.historyReadWatchAddr == 0100);
    CHECK(a.historyReadWatchLen  == 8);
}

}  // TEST_SUITE


TEST_SUITE("parseArgs — error handling") {

TEST_CASE("unknown flags don't crash (warning printed to stderr)") {
    /* The header guarantees parseArgs is forgiving — unknowns are
     * reported but don't abort.  Just exercise the path. */
    app::CliArgs a = parse({"--definitely-not-a-real-flag", "value"});
    CHECK(a.romPath.empty());
}

TEST_CASE("retired flags (--fd0..fd3, --disk, --drive) don't crash") {
    parse({"--fd0", "x.dsk"});
    parse({"--disk", "x.dsk"});
    parse({"--drive", "0"});
    MESSAGE("retired-arg path executes without throwing");
}

TEST_CASE("flags missing their value argument are ignored gracefully") {
    /* --rom at the end of argv has no value; parseArgs should not read
     * past argv. */
    app::CliArgs a = parse({"--rom"});
    CHECK(a.romPath.empty());
}

}  // TEST_SUITE


TEST_SUITE("mergeCliOverConfig") {

TEST_CASE("empty cli fields inherit from config") {
    app::Config cfg;
    cfg.romPath  = "rom-from-cfg";
    cfg.dsPath[0] = "ds-from-cfg";
    cfg.fdPath[2] = "fd2-from-cfg";
    cfg.hdPath    = "hd-from-cfg";

    app::CliArgs cli;            /* all empty */
    app::CliArgs merged = app::mergeCliOverConfig(std::move(cli), cfg);
    CHECK(merged.romPath   == "rom-from-cfg");
    CHECK(merged.dsPath[0] == "ds-from-cfg");
    CHECK(merged.fdPath[2] == "fd2-from-cfg");
    CHECK(merged.hdPath    == "hd-from-cfg");
}

TEST_CASE("non-empty cli hdPath wins over config") {
    app::Config cfg;
    cfg.hdPath = "hd-from-cfg";
    app::CliArgs cli;
    cli.hdPath = "hd-from-cli";
    app::CliArgs merged = app::mergeCliOverConfig(std::move(cli), cfg);
    CHECK(merged.hdPath == "hd-from-cli");
}

TEST_CASE("non-empty cli fields win over config") {
    app::Config cfg;
    cfg.romPath  = "rom-from-cfg";
    cfg.dsPath[0] = "ds-from-cfg";

    app::CliArgs cli;
    cli.romPath  = "rom-from-cli";
    cli.dsPath[0] = "ds-from-cli";

    app::CliArgs merged = app::mergeCliOverConfig(std::move(cli), cfg);
    CHECK(merged.romPath   == "rom-from-cli");
    CHECK(merged.dsPath[0] == "ds-from-cli");
}

TEST_CASE("a mix — cli for one drive, config for the other") {
    app::Config cfg;
    cfg.dsPath[1] = "ds1-cfg";
    cfg.romPath   = "rom-cfg";

    app::CliArgs cli;
    cli.dsPath[0] = "ds0-cli";

    app::CliArgs merged = app::mergeCliOverConfig(std::move(cli), cfg);
    CHECK(merged.dsPath[0] == "ds0-cli");
    CHECK(merged.dsPath[1] == "ds1-cfg");
    CHECK(merged.romPath   == "rom-cfg");
}

}  // TEST_SUITE
