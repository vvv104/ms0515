/*
 * test_diag.cpp - a look inside the engine while it plays: the variables
 * it exports, sampled every frame, printed when they change.  Not a test
 * of anything - the tool the port was debugged with; it runs only when
 * asked for by name (doctest -tc="*diag*").
 */
#include "TuneRun.hpp"

#include <cstdio>

using sab2tune::TuneRun;

TEST_CASE("sab2tune: diag - the engine's variables frame by frame" * doctest::skip(true))
{
    if (!sab2tune::built()) { MESSAGE("SABTUN not built - skipped"); return; }
    TuneRun run("sab2tune_diag");
    run.settle(250);
    std::string last;
    for (int i = 0; i < 1200; ++i) {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "SPTR %06o PERIOD %5u DUTY %5u STEP %6d LIM %u..%u LEVEL %02o AALT %3u BALT %u "
                      "SHIFT %u DMASK %03o SMASK %03o MODE %u ECHO %u JRALW %u PASS %03o SCNT %03o WACC %6d",
                      run.peek16("SPTR"), run.peek16("PERIOD"), run.peek16("DUTY"),
                      static_cast<int16_t>(run.peek16("STEP")), run.peek16("LIMLO"), run.peek16("LIMHI"),
                      run.peek8("LEVEL"), run.peek8("AALT"), run.peek8("BALT"), run.peek8("SHIFT"),
                      run.peek8("DMASK"), run.peek8("SMASK"), run.peek8("MODE"), run.peek8("ECHO"),
                      run.peek8("JRALW"), run.peek8("PASS"), run.peek8("SCNT"),
                      static_cast<int16_t>(run.peek16("WACC")));
        std::string now(buf);
        const int frame = i + 250;
        if (now.substr(0, 90) != last.substr(0, 90) || (frame >= 445 && frame <= 470))
            std::printf("frame %4d changes %5zu PC %06o SP %06o PSW %03o R0 %06o R1 %06o %s\n", frame,
                        run.changes.size(), run.board().cpu.r[7], run.board().cpu.r[6],
                        run.board().cpu.psw, run.board().cpu.r[0], run.board().cpu.r[1], buf);
        last = now;
        run.step();
    }
}
