/*
 * test_trace.cpp - an instruction trace of the engine at work: (PC, cycle)
 * pairs written to a file for the calibration of the path costs.  Not a
 * test; runs only when asked for by name (-tc="*trace*" -ns), and takes
 * --sab2tune-trace=<file> for where to write (default: sab2tune_trace.txt
 * in the temp directory) and --sab2tune-steps=<n> instructions (default
 * 60000), from the tune's first frames on.
 */
#include "TuneRun.hpp"

#include <fstream>

using sab2tune::TuneRun;

TEST_CASE("sab2tune: trace - PC and cycle of every instruction for a while" * doctest::skip(true))
{
    if (!sab2tune::built()) { MESSAGE("SABTUN not built - skipped"); return; }
    const std::string out = sab2tune::optOr("trace", (sab2tune::fs::temp_directory_path() / "sab2tune_trace.txt").string());
    const int steps = std::stoi(sab2tune::optOr("steps", "60000"));
    TuneRun run("sab2tune_trace");
    const int fromFrame = std::stoi(sab2tune::optOr("fromframe", "0"));
    if (fromFrame > 0) {
        run.settle(fromFrame);                 // --sab2tune-fromframe=N: from there, whatever plays
    } else {
        run.settle(300);
        run.changes.clear();
        const int wait = std::stoi(sab2tune::optOr("after", "40"));
        run.runUntilChanges(static_cast<size_t>(wait), 3000);
        REQUIRE(run.changes.size() >= static_cast<size_t>(wait));
        MESSAGE("first change at cycle " << run.changes[0].at);
    }
    MESSAGE("tracing from cycle " << run.board().total_cycles << ", frame " << run.frame());
    // --sab2tune-pokeaddr=<octal> --sab2tune-pokepc=<octal>: a byte set to
    // 1 whenever the PC is there - the frame tick's flag, which single
    // stepping never raises
    const int pokeAddr = std::stoi(sab2tune::optOr("pokeaddr", "0"), nullptr, 8);
    const int pokePc = std::stoi(sab2tune::optOr("pokepc", "0"), nullptr, 8);
    std::ofstream f(out);
    REQUIRE(f.is_open());
    for (int i = 0; i < steps; ++i) {
        if (pokeAddr && run.board().cpu.r[7] == static_cast<uint16_t>(pokePc))
            run.board().mem.ram[pokeAddr] = 1;
        f << std::oct << run.board().cpu.r[7] << std::dec << ' ' << run.board().total_cycles << ' '
          << run.board().cpu.r[0] << ' ' << run.board().cpu.r[1] << ' ' << run.board().cpu.r[2] << '\n';
        run.emu.stepInstruction();
    }
    f.close();
    MESSAGE("trace: " << out);
}
