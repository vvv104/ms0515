/*
 * test_tune.cpp - the tune played on the machine against the model's
 * rendering of the original.
 *
 * The reference (SAB2TN.REF) is rendered with this machine's frame and
 * the phase the engine starts at (it waits for a frame tick first), so
 * the frame interrupt falls at the same tick in both, and the note ends
 * and the drums with it.  The comparison is frame by frame: the level
 * changes between two ticks here are paired in order with the reference's
 * between the same two ticks, and each pair's interval - ours in clocks,
 * the reference's in T-states times 15/7 - must agree to a few turns of
 * the wait loop.  The interval that holds the tick itself, a drum's
 * clicks and the interval after them (the interrupt's own time) and the
 * first of a note (its setup) are not held to that.  The per-tag means
 * the test prints are how the engine's path costs were calibrated (the P
 * figures in SAB2TN.MAC).
 */
#include "TuneRun.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

using sab2tune::Change;
using sab2tune::TuneRun;

namespace {

struct TagStat { double sum = 0; double sumAbs = 0; int n = 0; double worst = 0; };

constexpr double kTight = 90;      /* clocks: three turns of the wait loop */

/* The first change at or after time t. */
size_t at(const std::vector<Change> &v, size_t from, double t)
{
    while (from < v.size() && static_cast<double>(v[from].at) < t) ++from;
    return from;
}

}  // namespace

TEST_CASE("sab2tune: the tune plays as the original does, level change by level change")
{
    if (!sab2tune::built()) { MESSAGE("SABTUN not built - skipped"); return; }
    const std::vector<Change> ref = sab2tune::reference();
    REQUIRE(ref.size() > 60000);

    TuneRun run("sab2tune_play");
    // the ROM's self-test plays its scale during the boot: past it, then
    // the program's start and the tone's first toggle
    run.settle(300);
    run.changes.clear();
    run.runUntilChanges(2, 3000);
    REQUIRE_MESSAGE(run.changes.size() >= 2, "the tune did not start");
    const int frames = run.runUntilChanges(ref.size() + 40, 4000);
    MESSAGE("changes: " << run.changes.size() << " in " << frames << " frames; reference " << ref.size());
    if (const std::string dump = sab2tune::optOr("dump", ""); !dump.empty()) {
        std::ofstream f(dump);                 // --sab2tune-dump=<file>: ours, "cycle level" lines,
        for (const auto &c : run.changes)      // then "F cycle" for every frame start
            f << c.at << ' ' << c.level << '\n';
        for (uint64_t fsc : run.frameStarts)
            f << "F " << fsc << '\n';
    }

    // align at the tone's first toggle (TNPLAY's "speaker low" write is no
    // change when the speaker was low already, so it is not counted on)
    size_t o = 0;
    while (o < run.changes.size() && run.changes[o].level != ref[0].level) ++o;
    REQUIRE(o < run.changes.size());
    const std::vector<Change> ours(run.changes.begin() + static_cast<long>(o), run.changes.end());
    const uint64_t origin = ours[0].at;
    std::vector<double> ourTicks;                      // clocks from the first toggle
    for (uint64_t f : run.frameStarts)
        if (f > origin) ourTicks.push_back(static_cast<double>(f - origin));
    std::vector<double> refTicks;                      // the same, from T-states
    for (size_t k = 0; k < ourTicks.size() + 1; ++k)
        refTicks.push_back((sab2tune::kFirstTickT + k * sab2tune::kFrameT - ref[0].at) * sab2tune::kClocksPerT);
    MESSAGE("the first tick " << ourTicks[0] << " clocks after the first toggle, the reference's " << refTicks[0]);

    std::map<std::string, TagStat> byTag;
    std::map<int, int> countDiff;
    int pairs = 0, tightBad = 0, levelBad = 0;
    size_t a0 = 0, r0 = 0;
    for (size_t k = 0; k + 1 < ourTicks.size() && k + 1 < refTicks.size(); ++k) {
        a0 = at(ours, a0, ourTicks[k]);
        const size_t a1 = at(ours, a0, ourTicks[k + 1]);
        r0 = at(ref, r0, refTicks[k] / sab2tune::kClocksPerT + ref[0].at);
        const size_t r1 = at(ref, r0, refTicks[k + 1] / sab2tune::kClocksPerT + ref[0].at);
        const long na = static_cast<long>(a1 - a0), nr = static_cast<long>(r1 - r0);
        ++countDiff[static_cast<int>(na - nr)];
        const size_t n = static_cast<size_t>(std::min(na, nr));
        for (size_t m = 1; m < n; ++m) {
            const size_t i = r0 + m, j = a0 + m;
            if (ref[i].level != ours[j].level) { ++levelBad; break; }
            const std::string &tag = ref[i].tag;
            if (tag == "drum" || ref[i - 1].tag == "drum" || tag[0] == '-') continue;
            const double dRef = static_cast<double>(ref[i].at - ref[i - 1].at) * sab2tune::kClocksPerT;
            const double dOur = static_cast<double>(ours[j].at - ours[j - 1].at);
            const double err = dOur - dRef;
            ++pairs;
            TagStat &s = byTag[tag];
            s.sum += err; s.sumAbs += std::fabs(err); ++s.n;
            if (std::fabs(err) > s.worst) s.worst = std::fabs(err);
            if (std::fabs(err) > kTight) {
                if (tightBad < 8)
                    MESSAGE("frame " << k << " pair " << m << " tag " << tag << ": ours " << dOur << " ref " << dRef
                            << " (" << err << ")");
                ++tightBad;
            }
        }
    }
    double worstMean = 0;
    for (const auto &[tag, s] : byTag) {
        MESSAGE("tag " << tag << ": n " << s.n << " mean " << s.sum / s.n << " mean|err| " << s.sumAbs / s.n
                << " worst " << s.worst);
        if (s.n >= 100 && std::fabs(s.sum / s.n) > worstMean) worstMean = std::fabs(s.sum / s.n);
    }
    int sameCount = 0, frameCount = 0;
    for (const auto &[d, n] : countDiff) { frameCount += n; if (d >= -1 && d <= 1) sameCount += n; }
    MESSAGE("pairs " << pairs << ", over " << kTight << " clocks: " << tightBad << ", level mismatches " << levelBad
            << "; frames " << frameCount << ", with the same changes (+-1) " << sameCount
            << "; worst mean of a path " << worstMean);
    CHECK(sameCount * 100 >= frameCount * 90);
    CHECK(levelBad < 40);
    CHECK(tightBad * 200 < pairs);          // under half a percent
    CHECK(worstMean < 25);                  // a path is not consistently off
}

TEST_CASE("sab2tune: a key stops the tune and the program leaves for RT-11")
{
    if (!sab2tune::built()) { MESSAGE("SABTUN not built - skipped"); return; }
    TuneRun run("sab2tune_key");
    run.settle(300);
    run.changes.clear();
    run.runUntilChanges(200, 3000);
    REQUIRE(run.changes.size() >= 200);
    run.keyTap(ms0515::Key::Space, 8);
    const size_t before = run.changes.size();
    run.settle(20);                                 // the check is every fifth frame
    const int quiet = run.runUntilQuiet(50, 200);
    CHECK(quiet < 200);                             // the speaker went quiet
    MESSAGE("changes after the key: " << run.changes.size() - before);
}
