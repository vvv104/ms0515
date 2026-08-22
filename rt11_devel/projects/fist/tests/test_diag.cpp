/*
 * test_diag.cpp - diagnostics on the running game, each opt-in through an
 * --fist-<name> option naming its output (they pass trivially otherwise).  Tooling for
 * the port, not checks: match-state traces with VRAM frame dumps, a PC
 * profile, key latency, the speaker waveform per sound code, a catalogue of
 * every move's animation.
 */
#include "FistGame.hpp"

#include <algorithm>
#include <string>

using fist::FistGame;

namespace {

void dumpTo(FistGame &g, const fist::fs::path &dir, const std::string &name)
{
    g.dumpVram(dir / (name + ".bin"));
}

/* One line of match state. */
void snap(std::ofstream &log, FistGame &g, int i, const char *why)
{
    log << i << ' ' << why << " dan=" << (int)g.gst(0xB05F) << " bg=" << (int)g.gst(0xAF34)
        << " rounds=" << (int)g.gst(0xAA3C) << " setup=" << (int)g.gst(0xAF35)
        << " yy=" << (int)g.gst(0xAA01) << "/" << (int)g.gst(0xAA41)
        << " pts=" << (int)g.gst(0xAA02) << "/" << (int)g.gst(0xAA42)
        << " act=" << (int)g.gst(0xAA04) << "/" << (int)g.gst(0xAA44)
        << " react=" << (int)g.gst(0xAA03) << "/" << (int)g.gst(0xAA43)
        << " 9C28=" << (int)g.gst(0x9C28) << " 9C2B=" << (int)g.gst(0x9C2B)
        << " clock=" << (int)g.gst(0x9CA5) << " x=" << (int)g.gst(0xAA19) << "/" << (int)g.gst(0xAA59)
        << " score=" << std::hex << (int)g.gst(0xB02F) << (int)g.gst(0xB02E) << (int)g.gst(0xB02D)
        << std::dec << '\n';
}

}  // namespace

// --fist-match-log=<file>: drive a long fight and log every match-state
// transition.  --fist-match-dumps=<dir>: VRAM frames at round ends and around
// the exchange resets (a ring of the frames before one).  --fist-match-back:
// P1 only walks back (the auto-block study).  --fist-frames=<n>: length.
TEST_CASE("fist: match transition log (diagnostic)")
{
    std::string logPath = fist::opt("match-log");
    if (logPath.empty() || !fist::built()) { MESSAGE("--fist-match-log not given - skipping"); return; }
    FistGame g("fist_match_lib");
    g.startGame();
    std::ofstream log(logPath);
    std::string dumpDir = fist::opt("match-dumps");
    bool holdBack = !fist::opt("match-back").empty();
    std::string fe = fist::opt("frames");
    int frames = fe.empty() ? 90000 : std::atoi(fe.c_str());
    std::vector<std::vector<uint8_t>> ring(10);
    size_t ringPos = 0;
    int dumpN = 0, resetAt = -1, resets = 0, blocks = 0;
    int pDan = -1, pBg = -1, pRounds = -1, pYY = -1, pFlags = -1;
    bool pStart = false;
    for (int i = 0; i < frames; ++i) {
        if (holdBack) {
            g.emu.keyPress(ms0515::Key::Left, true);
        } else {
            bool atk = (i / 40) % 2 == 0;
            g.emu.keyPress(ms0515::Key::Space, atk);
            g.emu.keyPress(ms0515::Key::Right, !atk);
        }
        g.step();
        if (g.gst(0xAA04) == 19 || g.gst(0xAA04) == 20) ++blocks;
        int dan = g.gst(0xB05F), bg = g.gst(0xAF34), rounds = g.gst(0xAA3C);
        int yy = g.gst(0xAA01) * 16 + g.gst(0xAA41);
        int flags = g.gst(0x9C28) * 256 + g.gst(0x9C2B);
        if (dan != pDan) snap(log, g, i, "DAN");
        else if (bg != pBg) snap(log, g, i, "BG");
        else if (rounds != pRounds) snap(log, g, i, "ROUNDS");
        else if (yy != pYY) snap(log, g, i, "YY");
        else if (flags != pFlags) snap(log, g, i, "FLAG");
        pDan = dan; pBg = bg; pRounds = rounds; pYY = yy; pFlags = flags;
        if (dumpDir.empty()) continue;
        // an exchange reset (RSTFRM: both at the start stance) after a score:
        // dump the ring of frames before it and a few after
        bool atStart = g.gst(0xAA19) == 32 && g.gst(0xAA59) == 60
                       && g.gst(0xAA04) == 23 && g.gst(0xAA44) == 23;
        if (atStart && !pStart && resets < 3) {
            resetAt = i; ++resets;
            for (size_t r = 0; r < ring.size(); ++r) {
                auto &fr = ring[(ringPos + r) % ring.size()];
                if (!fr.empty())
                    fist::writeFile(fist::fs::path(dumpDir) / ("m" + std::to_string(dumpN++) + "_pre"
                                    + std::to_string(r) + "_" + std::to_string(i) + ".bin"),
                                    fr.data(), fr.size());
            }
        }
        pStart = atStart;
        if (resetAt >= 0 && i - resetAt <= 12 && (i - resetAt) % 2 == 0)
            dumpTo(g, dumpDir, "m" + std::to_string(dumpN++) + "_post_" + std::to_string(i));
        ring[ringPos].assign(g.vram(), g.vram() + MEM_VRAM_SIZE);
        ringPos = (ringPos + 1) % ring.size();
    }
    snap(log, g, frames, "END");
    MESSAGE("frames with P1 in a block action (19 low / 20 high): " << blocks);
    CHECK(true);
}

// --fist-profile-out=<file>: sample the PC per instruction during a real fight
// (--fist-profile-steps, default 3M) - "octal count" per distinct PC.  A
// FIST_SYMTAB=1 build + source/profile_agg.py map the samples to routines.
TEST_CASE("fist: PC profile (diagnostic)")
{
    std::string outPath = fist::opt("profile-out");
    if (outPath.empty() || !fist::built()) { MESSAGE("--fist-profile-out not given - skipping"); return; }
    FistGame g("fist_prof_lib", 1500);
    g.startGame();
    std::string se = fist::opt("profile-steps");
    long steps = se.empty() ? 3000000L : std::atol(se.c_str());
    std::vector<uint32_t> hist(65536, 0);
    auto &board = g.board();
    auto &cpu = g.cpu();
    for (long i = 0; i < steps; ++i) {
        if (i % 20000 == 0) {                       // keys + a device tick (the UART)
            bool atk = (i / 800000) % 2 == 0;
            g.emu.keyPress(ms0515::Key::Space, atk);
            g.emu.keyPress(ms0515::Key::Right, !atk);
            g.step();
        }
        hist[cpu_get_pc(&cpu)]++;
        board_step_cpu(&board);
    }
    std::ofstream o(outPath);
    for (int pc = 0; pc < 65536; ++pc)
        if (hist[pc]) o << std::oct << pc << ' ' << std::dec << hist[pc] << '\n';
    CHECK(true);
}

namespace {

void traceReaction(FistGame &g, const char *phase, int n)
{
    int px = g.gst(0xAA19), pm = g.gst(0xAA05), pa = g.gst(0xAA04), f0 = g.frame();
    for (int i = 0; i < n; ++i) {
        g.step();
        int x = g.gst(0xAA19), m = g.gst(0xAA05), a = g.gst(0xAA04);
        if (x != px || m != pm || a != pa)
            MESSAGE(std::string(phase) << " +" << (g.frame() - f0) << " frames ("
                    << (g.frame() - f0) * 20 << " ms): x=" << x << " move=" << m << " act=" << a);
        px = x; pm = m; pa = a;
    }
}

}  // namespace

// --fist-keylat: press / release latency in video frames (P2 parked).
TEST_CASE("fist: key latency (diagnostic)")
{
    if (fist::opt("keylat").empty() || !fist::built()) { MESSAGE("--fist-keylat not given - skipping"); return; }
    FistGame g("fist_keylat_lib");
    g.startGame();
    g.parkP2();
    g.settle(100);
    g.emu.keyPress(ms0515::Key::Right, true);
    traceReaction(g, "PRESS", 60);
    g.emu.keyPress(ms0515::Key::Right, false);
    traceReaction(g, "RELEASE", 200);
    g.emu.keyPress(ms0515::Key::Space, true);
    traceReaction(g, "SPACE-PRESS", 40);
    g.emu.keyPress(ms0515::Key::Space, false);
    traceReaction(g, "SPACE-RELEASE", 200);
    CHECK(true);
}

// --fist-sndlog: play each sound code by poking $B150 and record the speaker
// transitions (reg C bit 5 bit-banging) with CPU-cycle stamps.
TEST_CASE("fist: sound effects (diagnostic)")
{
    if (fist::opt("sndlog").empty() || !fist::built()) { MESSAGE("--fist-sndlog not given - skipping"); return; }
    FistGame g("fist_snd_lib");
    g.startGame();
    g.parkP2();
    struct Rec { uint64_t cyc; int lvl; };
    std::vector<Rec> log;
    uint64_t cycles = 0;
    struct Ctx { std::vector<Rec> *log; uint64_t *cyc; } ctx{&log, &cycles};
    auto &board = g.board();
    auto &cpu = g.cpu();
    board_set_sound_callback(&board,
        [](void *u, int v) { auto *c = static_cast<Ctx *>(u); c->log->push_back({*c->cyc, v}); },
        &ctx);
    for (int code = 1; code <= 6; ++code) {
        log.clear();
        g.poke(0xAA45, 1);
        g.poke(0xB150, (uint8_t)code);
        uint64_t lastEvent = cycles, start = cycles;
        bool started = false;
        while (cycles - start < 60'000'000ULL) {          // 8 s budget
            board_step_cpu(&board);
            cycles += (uint64_t)cpu.cycles;
            if (!log.empty() && log.back().cyc != lastEvent) { lastEvent = log.back().cyc; started = true; }
            if (started && cycles - lastEvent > 1'000'000ULL) break;
        }
        if (log.size() < 2) { MESSAGE("code " << code << ": no speaker transitions"); continue; }
        double sum = 0; uint64_t mn = ~0ULL, mx = 0; int gaps = 0;
        for (size_t k = 1; k < log.size(); ++k) {
            uint64_t d = log[k].cyc - log[k - 1].cyc;
            if (d > 2'000'000ULL) { ++gaps; continue; }
            sum += (double)d; mn = std::min(mn, d); mx = std::max(mx, d);
        }
        double n = (double)(log.size() - 1 - gaps);
        MESSAGE("code " << code << ": transitions=" << log.size()
                << " half-period cycles mean=" << (long)(sum / n) << " min=" << mn << " max=" << mx
                << " pauses=" << gaps << " total=" << (log.back().cyc - log.front().cyc) / 7500 << " ms");
    }
    CHECK(true);
}

// --fist-moves-dir=<dir> with a FIST_DBGMOVE=1 build: play every P1 move 2..21
// through the $B156 override and dump VRAM frames of it (mv<N>_<k>.bin).
TEST_CASE("fist: move catalogue (diagnostic)")
{
    std::string outDir = fist::opt("moves-dir");
    if (outDir.empty() || !fist::built()) { MESSAGE("--fist-moves-dir not given - skipping"); return; }
    FistGame g("fist_moves_lib");
    g.startGame();
    g.parkP2();
    g.settle(60);
    for (int mv = 2; mv <= 21; ++mv) {
        g.resetFighters();
        g.settle(30);
        int n = 0, x0 = g.gst(0xAA19), f0 = g.gst(0xAA17);
        std::string acts;
        for (int i = 0; i < 72; ++i) {
            g.poke(0xB156, (uint8_t)(i < 40 ? mv : 0));   // hold the move ~40 frames
            g.step();
            if (i % 4 == 0 && i < 48) {
                dumpTo(g, outDir, "mv" + std::to_string(mv) + "_" + std::to_string(n++));
                acts += std::to_string((int)g.gst(0xAA04)) + " ";
            }
        }
        MESSAGE("move " << mv << ": P1 actions " << acts << " x " << x0 << "->" << (int)g.gst(0xAA19)
                << " facing " << f0 << "->" << (int)g.gst(0xAA17));
        g.poke(0xB156, 0);
    }
    CHECK(true);
}
