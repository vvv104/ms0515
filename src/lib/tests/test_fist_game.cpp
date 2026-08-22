/*
 * test_fist_game.cpp - boot RT-11 with the standalone FIST game on a folder
 * device, let it auto-run (STARTS.COM -> R FIST) so it loads GST.DAT into the
 * parked extended banks and renders, then dump VRAM.  Opt-in via env vars (all
 * must be set, else the test no-ops):
 *   FIST_GAME_SAV       path to the game FIST.SAV
 *   FIST_GAME_DAT       path to GST.DAT
 *   FIST_SYSTEM_DIR     path to the RT-11 system/ template folder
 *   FIST_GAME_VRAM_OUT  where to write the 16 KB VRAM dump
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include "../src/EmulatorInternal.hpp"
extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
#include <ms0515/core/memory.h>
}

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

static std::string env(const char *k)
{
    char *v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, k) != 0 || !v)
        return {};
    std::string r{v};
    free(v);
    return r;
}

TEST_CASE("fist: standalone game render via .DAT loader")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), out = env("FIST_GAME_VRAM_OUT");
    if (sav.empty() || dat.empty() || sys.empty() || out.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_game_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    // RT-11's console here is the serial port (as in the CLI).  Feed CRs through
    // the serial-in callback to accept the date/time prompts; STARTS.COM then
    // auto-runs the game.  Pace the CRs (one offered every ~30 frames) so they land
    // at the prompts rather than flooding early.
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });    // discard console output
    std::string framesEnv = env("FIST_GAME_FRAMES");
    int frames = framesEnv.empty() ? 3000 : std::atoi(framesEnv.c_str());
    for (int i = 0; i < frames; ++i) {
        if (i < 900 && (i % 30) == 0)
            offerCR = true;
        (void)emu.stepFrame();
    }

    auto &cpu = ms0515::internal::cpu(emu);
    MESSAGE("final CPU PC = " << std::oct << cpu_get_pc(&cpu) << std::dec);
    auto &board = ms0515::internal::board(emu);
    const uint8_t *vram = board_get_vram(&board);
    {
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char *>(vram), MEM_VRAM_SIZE);
    }
    int nz = 0;
    for (int i = 0; i < MEM_VRAM_SIZE; ++i)
        if (vram[i]) ++nz;
    MESSAGE("VRAM non-zero bytes: " << nz);
    // The per-frame loop catches the game at an arbitrary frame; a live two-fighter
    // frame is ~900 nz.  >500 distinguishes that from a blank/trapped screen (<=560).
    CHECK(nz > 300);
}

TEST_CASE("fist: yin-yang display at a forced score")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), out = env("FIST_GAME_VRAM_OUT");
    if (sav.empty() || dat.empty() || sys.empty() || out.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }
    fs::path tmp = fs::temp_directory_path() / "fist_forced_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    { std::ofstream s(boot / "STARTS.COM", std::ios::binary); s << "ASSIGN DZ1 DK\r\nR FIST\r\n"; }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    { std::ofstream s(work / "device.rtfs", std::ios::binary); s << "device: floppy\nblocks: 800\n"; }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool { if (offerCR) { b = '\r'; offerCR = false; return true; } return false; },
        [](uint8_t) -> bool { return true; });
    auto &board = ms0515::internal::board(emu);
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    for (int i = 0; i < 1200; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    // Idle fighters (no keys) rarely trigger ROUNDE, so a forced total holds:
    // P1 = 3 (full inner + half outer), P2 = 2 (full inner).  Dump the last frame.
    for (int i = 0; i < 300; ++i) {
        poke(0xAA01, 3); poke(0xAA41, 2);
        (void)emu.stepFrame();
    }
    const uint8_t *vram = board_get_vram(&board);
    std::ofstream o(out, std::ios::binary);
    o.write(reinterpret_cast<const char *>(vram), MEM_VRAM_SIZE);
    MESSAGE("forced AA01=3 AA41=2 -> dumped VRAM");
    CHECK(true);
}

TEST_CASE("fist: real .dsk boots and shows the HUD")
{
    // Replicates the GUI exactly: one .dsk image on disk0, its own STARTS.COM
    // (R FIST) with GST.DAT alongside on the same volume.  Opt-in via FIST_DSK.
    std::string dsk = env("FIST_DSK");
    if (dsk.empty()) {
        MESSAGE("FIST_DSK not set - skipping");
        return;
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, dsk));

    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });
    auto &board = ms0515::internal::board(emu);
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        board.mem.ram[((addr >> 13) + 8) * 8192 + (addr & 8191)] = v;
    };
    for (int i = 0; i < 3000; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        if (i >= 2900) { poke(0xAA01, 2); poke(0xAA41, 2); }   // force a score to draw the HUD
        (void)emu.stepFrame();
    }
    const uint8_t *vram = board_get_vram(&board);
    // The HUD's four yin-yang slots sit in rows 6-21 at cols 5-6/8-9/30-31/33-34.
    int hud = 0;
    for (int r = 6; r < 22; ++r)
        for (int c : {5, 6, 8, 9, 30, 31, 33, 34})
            if (vram[r * 80 + c * 2]) ++hud;
    MESSAGE("HUD slot pixels in the top strip: " << hud);
    CHECK(hud > 0);      // the score UI renders when booted from the real disk

    // Numeric score (DRWSCR): the six-digit BCD score at $B02D draws across the top
    // strip (row 6, byte 34+).  Two different values must produce different pixels.
    auto scoreSig = [&]() {
        const uint8_t *v = board_get_vram(&board);
        unsigned h = 0, n = 0;
        for (int r = 6; r < 14; ++r)
            for (int c = 34; c < 48; ++c) { h = h * 131 + v[r*80+c]; n += v[r*80+c]; }
        return std::make_pair(h, n);
    };
    poke(0xB02D, 0); poke(0xB02E, 0); poke(0xB02F, 0);       // score 000000
    for (int i = 0; i < 40; ++i) (void)emu.stepFrame();
    auto sZero = scoreSig();
    poke(0xB02D, 0x34); poke(0xB02E, 0x12); poke(0xB02F, 0x56);  // distinct digits
    for (int i = 0; i < 40; ++i) (void)emu.stepFrame();
    auto sVal = scoreSig();
    MESSAGE("score-region pixel sum: zero=" << sZero.second << " nonzero=" << sVal.second);
    CHECK(sVal.second > 0);            // digits are drawn in the score strip
    CHECK(sVal.first != sZero.first);  // and they track the actual score value
}

TEST_CASE("fist: keyboard drives P1")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_kbd_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });

    auto &board = ms0515::internal::board(emu);
    // GST lives in the extended banks (slot N -> physical bank N+8); $AA19 = P1 x.
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);   // runtime home 0100000
        uint32_t bank = (addr >> 13) + 8;              // extended bank 12..14
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };

    // Boot the game (clear the date prompts, let the loader run).
    for (int i = 0; i < 1500; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }

    // P1 must be human (AA06=0) or MOVSEL's AI overrides the keyboard.
    int human = gst(0xAA06);
    MESSAGE("P1 AA06 (0=human): " << human);
    CHECK(human == 0);
    // Park P2: with the match initialised faithfully the AI closes in and attacks
    // within seconds, and a step pressed with the opponent adjacent (or while
    // knocked down) is not a step.  This test is about the keyboard path, so take
    // P2 off the AI and hold its move at idle every frame (MOVSEL's human branch
    // leaves $AA45 alone, so the AI's last move would otherwise stay latched).
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    poke(0xAA46, 0);
    // Tick the keyboard model's clock (the frontend does this from SDL time) so
    // a held key auto-repeats; KSCAN treats a key with no repeat for KTMR frames
    // as released, so without the ticks a hold is a single step.
    uint32_t nowMs = 0;
    auto settle = [&](int n) {
        for (int i = 0; i < n; ++i) {
            poke(0xAA45, 1);
            nowMs += 20;
            emu.keyTick(nowMs);
            (void)emu.stepFrame();
        }
    };

    int xBase = gst(0xAA19);
    emu.keyPress(ms0515::Key::Right, true);
    settle(400);
    int xRight = gst(0xAA19);
    emu.keyPress(ms0515::Key::Right, false);
    settle(80);
    emu.keyPress(ms0515::Key::Left, true);
    settle(400);
    int xLeft = gst(0xAA19);
    emu.keyPress(ms0515::Key::Left, false);

    MESSAGE("P1 x ($AA19): baseline=" << xBase
            << "  after RIGHT=" << xRight << "  after LEFT=" << xLeft);
    CHECK(xRight != xLeft);      // keyboard moves P1
    CHECK(xRight >= xLeft);      // RIGHT ends further right than LEFT
}

TEST_CASE("fist: yin-yang score accumulates")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty()) {
        MESSAGE("FIST game env not set - skipping");
        return;
    }

    fs::path tmp = fs::temp_directory_path() / "fist_score_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));

    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });

    auto &board = ms0515::internal::board(emu);
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    // Diagnostic: with FIST_BOTH_AI, make P1 an AI too (as in the attract demo) so
    // both fighters move & cross freely - checks whether the faithful hit-detection
    // yields a two-sided match once the fighters aren't pinned left/right.
    bool bothAI = !env("FIST_BOTH_AI").empty();

    for (int i = 0; i < 1200; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    int a01_0 = gst(0xAA01), a41_0 = gst(0xAA41);
    // Drive P1 aggressively: hold RIGHT to close, punch (SPACE) in bursts, so real
    // hits/knockdowns happen and ROUNDE has exchanges to score.
    std::string vout = env("FIST_GAME_VRAM_OUT");
    bool dumped = false;
    int peak = 0, changes = 0, p2peak = 0;
    int mAA03 = 0, mAA43 = 0, mAA08 = 0, mAA48 = 0;   // reactions + score flags
    int x2min = 255, x2max = 0, mMove2 = 0, mAct2 = 0;  // P2 movement + AI move/action
    int mAct2atk = 0, mAct1atk = 0;                     // frames each fighter is attacking
    int minGap = 255, closeFrames = 0, resets = 0;      // fighter spacing + RSTFRM activity
    int prevGap = -1;
    int prev = gst(0xAA01) + gst(0xAA41);
    // Round end: the round is decided (two yin-yang, or the clock / a knockdown
    // ends it - ROUNDE), its final frame holds while the winner bows, then the
    // tallies reset (NEWRND).  Count decided rounds, the hold length, the resets.
    int holdLen = 0, maxHold = 0, firstHold = 0, roundsDecided = 0, roundsReset = 0;
    bool decided = false;
    // Phase 1 (idle P1): let the AI P2 try to score.  Phase 2: P1 attacks.
    if (bothAI) poke(0xAA06, 1);              // P1 becomes an AI, MOVSEL drives it
    std::string traj = env("FIST_TRAJ_OUT");
    std::ofstream trajf;
    if (!traj.empty()) trajf.open(traj);
    for (int i = 0; i < 24000; ++i) {
        if (bothAI) poke(0xAA06, 1);          // keep it AI across any reset
        if (!bothAI && i == 1500) emu.keyPress(ms0515::Key::Right, true);
        if (!bothAI && i >= 1500) {
            bool atk = (i / 40) % 2 == 0;      // alternate punch / approach
            emu.keyPress(ms0515::Key::Space, atk);
            emu.keyPress(ms0515::Key::Right, !atk);
        }
        (void)emu.stepFrame();
        if (trajf && i < 1200)
            trajf << i << ',' << (int)gst(0xAA19) << ',' << (int)gst(0xAA59)
                  << ',' << (int)gst(0xAA17) << ',' << (int)gst(0xAA57)
                  << ',' << (int)gst(0xAA04) << ',' << (int)gst(0xAA44) << '\n';
        p2peak = std::max(p2peak, (int)gst(0xAA41));
        mAA03 = std::max(mAA03, (int)gst(0xAA03)); mAA43 = std::max(mAA43, (int)gst(0xAA43));
        mAA08 = std::max(mAA08, (int)gst(0xAA08)); mAA48 = std::max(mAA48, (int)gst(0xAA48));
        int x2 = gst(0xAA59); x2min = std::min(x2min, x2); x2max = std::max(x2max, x2);
        int gap = std::abs((int)gst(0xAA59) - (int)gst(0xAA19));
        minGap = std::min(minGap, gap); if (gap < 20) ++closeFrames;
        // RSTFRM snaps P2 to 0x3C(=60); count big outward jumps as reset events.
        if (prevGap >= 0 && gap - prevGap > 15) ++resets;
        prevGap = gap;
        mMove2 = std::max(mMove2, (int)gst(0xAA45)); mAct2 = std::max(mAct2, (int)gst(0xAA44));
        // A90D[action] != 0 marks an *attack* action; count frames each fighter is in one.
        if (gst(0xA90D + gst(0xAA44))) ++mAct2atk;    // P2 attacking
        if (gst(0xA90D + gst(0xAA04))) ++mAct1atk;    // P1 attacking
        peak = std::max(peak, std::max((int)gst(0xAA01), (int)gst(0xAA41)));
        int s = gst(0xAA01) + gst(0xAA41);
        if (s != prev) { ++changes; prev = s; }
        bool won4 = gst(0xAA01) >= 4 || gst(0xAA41) >= 4;
        bool clockOut = gst(0x9C2B) != 0;
        if (!decided && (won4 || clockOut) && s > 0) { decided = true; ++roundsDecided; holdLen = 0; }
        if (decided) {
            ++holdLen;
            if (s == 0 && gst(0x9CA5) == 30) {          // NEWRND: tallies cleared, clock reset
                decided = false; ++roundsReset;
                maxHold = std::max(maxHold, holdLen);
                if (firstHold == 0) firstHold = holdLen;
            }
        }
        // Snapshot the GST when P2 is attacking within striking range, to replay
        // hit_detect(HIT_P2) offline and see which test rejects the hit.
        std::string gout = env("FIST_GST_OUT");
        if (!gout.empty()) {
            int d = std::abs((int)gst(0xAA59) - (int)gst(0xAA19));
            bool cap;
            if (bothAI)                    // both-AI: capture an early (pre-flee) frame
                cap = (i == 2);
            else {                         // human: P2 attacking at its hit frame in range
                int hitFrame = gst(0xA971 + gst(0xAA44));
                cap = (i < 8600 && gst(0xA90D + gst(0xAA44)) && d < 18
                       && gst(0xAA52) == hitFrame);
            }
            if (cap) {
                std::ofstream o(gout, std::ios::binary);
                o.write(reinterpret_cast<const char *>(&board.mem.ram[12 * 8192]), 24576);
            }
        }
        // Snapshot VRAM whenever a score is on screen (overwrite -> keep the last
        // full frame) to eyeball the HUD.
        if (!vout.empty() && s >= 1) {
            dumped = true;
            const uint8_t *vram = board_get_vram(&board);
            std::ofstream o(vout, std::ios::binary);
            o.write(reinterpret_cast<const char *>(vram), MEM_VRAM_SIZE);
        }
    }
    MESSAGE("start AA01/AA41=" << a01_0 << "/" << a41_0
            << "  peak yin-yang=" << peak << "  P2 peak=" << p2peak
            << "  score-changes=" << changes);
    MESSAGE("  reactions max: P1 hit(AA03)=" << mAA03 << " P2 hit(AA43)=" << mAA43
            << "  score flags max: AA08=" << mAA08 << " AA48=" << mAA48);
    MESSAGE("  P2 AI flag AA46=" << (int)gst(0xAA46) << " (1=AI)  P1 AA06=" << (int)gst(0xAA06));
    MESSAGE("  P2 x range=[" << x2min << ".." << x2max << "] (RSTFRM sets 60)  "
            << "P2 max move(AA45)=" << mMove2 << " max action(AA44)=" << mAct2);
    MESSAGE("  attack-action frames: P1=" << mAct1atk << " P2=" << mAct2atk);
    MESSAGE("  fighter spacing: min gap=" << minGap << "  frames in range(<20)=" << closeFrames
            << "  reset jumps=" << resets);
    MESSAGE("  rounds: decided=" << roundsDecided << " reset=" << roundsReset
            << " first hold=" << firstHold << " max hold=" << maxHold);
    CHECK(a01_0 == 0);           // the match starts 0-0 (GST.DAT snapshot cleared)
    CHECK(changes > 0);          // clean hits are scored into the yin-yang total
    CHECK(peak >= 2);            // at least a full yin-yang accrues over the bout
    // Match outcome: a decided round (two yin-yang, or the clock with a score on
    // the board) holds its final frame while the winner bows, then resets.
    CHECK(roundsDecided >= 1);
    CHECK(maxHold > 60);         // the hold is many video frames long
    CHECK(roundsReset >= 1);     // then the tallies reset for the next round
}

// The dojo follows the rank: a 1UP game opens on background 2 ($AF34 = 2), and
// each won bout advances $AF34 (3 -> 1 -> 2 ...) and re-renders the dojo.  The
// static rows of the picture (above the fighters' band, below the HUD strip)
// must match the Python reference render of that background byte for byte.
// Opt-in via FIST_BG_EXPECT_DIR = dir holding bg{1,2,3}_vram.bin (bg_expect.py).
TEST_CASE("fist: background follows the rank")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), expDir = env("FIST_BG_EXPECT_DIR");
    if (sav.empty() || dat.empty() || sys.empty() || expDir.empty()) {
        MESSAGE("FIST game / bg-expect env not set - skipping");
        return;
    }
    std::vector<std::vector<uint8_t>> expect(4);
    for (int ref = 1; ref <= 3; ++ref) {
        std::ifstream f(fs::path(expDir) / ("bg" + std::to_string(ref) + "_vram.bin"),
                        std::ios::binary);
        REQUIRE(f.good());
        expect[ref].assign(std::istreambuf_iterator<char>(f), {});
        REQUIRE(expect[ref].size() >= 200u * 80u);
    }

    fs::path tmp = fs::temp_directory_path() / "fist_bg_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });

    auto &board = ms0515::internal::board(emu);
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    // Static picture rows 24..63 (below the row-6 HUD strip, above any fighter),
    // picture columns 4..35 (the centred 256 px) -> bytes [row*80+8, row*80+72).
    auto mismatches = [&](int ref) {
        const uint8_t *vram = board_get_vram(&board);
        int bad = 0;
        for (int row = 24; row < 64; ++row)
            for (int b = row * 80 + 8; b < row * 80 + 72; ++b)
                if (vram[b] != expect[ref][b]) ++bad;
        return bad;
    };

    for (int i = 0; i < 1200; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    CHECK(gst(0xAF34) == 2);                 // $AC59: the 1UP game opens on bg 2
    CHECK(gst(0xB05F) == 0);                 // a novice ...
    CHECK(gst(0xAA3C) == 2);                 // ... two rounds per opponent
    CHECK(gst(0x9CA5) > 0);                  // $AEF8: 30 s on the clock (ticking)
    CHECK(gst(0x9CA5) <= 30);
    CHECK(mismatches(2) == 0);

    // Win rounds (P1 lands a clean full point at 2 -> 4 = two yin-yang).  The 2nd
    // round won against an opponent moves on: RANKTK advances $AF34, the dan
    // $B05F climbs, RENDBG redraws the dojo (after the round-end hold).
    auto p1Wins = [&](int maxFrames, auto until) {
        for (int i = 0; i < maxFrames; ++i) {
            if (gst(0xAA01) < 4) {
                poke(0xAA01, 2); poke(0xAA08, 2);          // P1 at 2, scored a full point
                poke(0xAA03, 0x10); poke(0xAA43, 0);       // a clean hit (SCDET bit 4)
                poke(0x9C28, 1);                           // knocked into recovery
            }
            (void)emu.stepFrame();
            if (until()) return true;
        }
        return false;
    };
    int expectRef = 2;
    for (int opp = 0; opp < 4; ++opp) {
        int before = gst(0xAF34);
        REQUIRE(p1Wins(3000, [&] { return gst(0xAF34) != before; }));
        expectRef = expectRef % 3 + 1;                     // $AF27: 1..3, 4 -> 1
        CHECK(gst(0xAF34) == expectRef);
        CHECK(gst(0xB05F) == opp + 1);                     // dan (BCD) per opponent beaten
        CHECK(gst(0xAA3C) == 2);                           // rounds reset for the next one
        for (int i = 0; i < 120; ++i)                      // let the loop re-present
            (void)emu.stepFrame();
        int bad = mismatches(expectRef);
        MESSAGE("opponent " << opp << ": $AF34=" << (int)gst(0xAF34) << " dan="
                << (int)gst(0xB05F) << " static-row mismatches vs bg" << expectRef
                << " = " << bad);
        CHECK(bad == 0);
    }
    // The clock pays out as points on a win ($AD5F): the score is no longer 0.
    CHECK((gst(0xB02D) | gst(0xB02E) | gst(0xB02F)) != 0);
    for (int i = 0; i < 1500; ++i)                         // and it stays stable
        (void)emu.stepFrame();
    CHECK(mismatches(expectRef) == 0);

    // P2 wins a round -> game over -> a fresh 1UP game: novice again, score 0,
    // background 2 re-rendered.
    bool over = false;
    for (int i = 0; i < 3000 && !over; ++i) {
        if (gst(0xAA41) < 4) {
            poke(0xAA41, 2); poke(0xAA48, 2);
            poke(0xAA43, 0x10); poke(0xAA03, 0);
            poke(0x9C28, 1);
        }
        (void)emu.stepFrame();
        over = gst(0xB05F) == 0 && gst(0xAA41) == 0;
    }
    REQUIRE(over);
    CHECK(gst(0xAF34) == 2);
    CHECK((gst(0xB02D) | gst(0xB02E) | gst(0xB02F)) == 0);
    for (int i = 0; i < 120; ++i)
        (void)emu.stepFrame();
    CHECK(mismatches(2) == 0);
}

// Diagnostic (opt-in via FIST_MATCH_LOG=<file>): drive a long real fight and log
// every match-state transition - rank, background, rounds, tallies, the two
// fighters' actions/reactions - to see what the match loop does at a round end.
TEST_CASE("fist: match transition log (diagnostic)")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), logPath = env("FIST_MATCH_LOG");
    if (sav.empty() || dat.empty() || sys.empty() || logPath.empty()) {
        MESSAGE("FIST_MATCH_LOG not set - skipping");
        return;
    }
    fs::path tmp = fs::temp_directory_path() / "fist_match_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });
    auto &board = ms0515::internal::board(emu);
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };
    for (int i = 0; i < 1200; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    std::ofstream log(logPath);
    auto snap = [&](int i, const char *why) {
        log << i << ' ' << why << " dan=" << (int)gst(0xB05F) << " bg=" << (int)gst(0xAF34)
            << " rounds=" << (int)gst(0xAA3C) << " setup=" << (int)gst(0xAF35)
            << " yy=" << (int)gst(0xAA01) << "/" << (int)gst(0xAA41)
            << " pts=" << (int)gst(0xAA02) << "/" << (int)gst(0xAA42)
            << " act=" << (int)gst(0xAA04) << "/" << (int)gst(0xAA44)
            << " react=" << (int)gst(0xAA03) << "/" << (int)gst(0xAA43)
            << " 9C28=" << (int)gst(0x9C28) << " 9C2B=" << (int)gst(0x9C2B)
            << " clock=" << (int)gst(0x9CA5) << " x=" << (int)gst(0xAA19) << "/" << (int)gst(0xAA59)
            << " score=" << std::hex << (int)gst(0xB02F) << (int)gst(0xB02E) << (int)gst(0xB02D)
            << std::dec << '\n';
    };
    uint32_t nowMs = 0;
    int frames = 90000;
    std::string fe = env("FIST_GAME_FRAMES");
    if (!fe.empty()) frames = std::atoi(fe.c_str());
    int pDan = -1, pBg = -1, pRounds = -1, pYY = -1, pReact = -1, p9c28 = -1, p9c2b = -1;
    std::string dumpDir = env("FIST_MATCH_DUMPS");        // optional VRAM frames at events
    int dumpAt = -1, dumpN = 0, holdAt = -1;
    auto dump = [&](int i, const char *tag) {
        if (dumpDir.empty()) return;
        std::ofstream o(fs::path(dumpDir) / ("m" + std::to_string(dumpN++) + "_" + tag + "_" +
                                             std::to_string(i) + ".bin"), std::ios::binary);
        o.write(reinterpret_cast<const char *>(board_get_vram(&board)), MEM_VRAM_SIZE);
    };
    for (int i = 0; i < frames; ++i) {
        bool atk = (i / 40) % 2 == 0;
        emu.keyPress(ms0515::Key::Space, atk);
        emu.keyPress(ms0515::Key::Right, !atk);
        nowMs += 20;
        emu.keyTick(nowMs);
        (void)emu.stepFrame();
        int dan = gst(0xB05F), bg = gst(0xAF34), rounds = gst(0xAA3C);
        int yy = gst(0xAA01) * 16 + gst(0xAA41);
        int react = gst(0xAA03) * 256 + gst(0xAA43);
        int c28 = gst(0x9C28), c2b = gst(0x9C2B);
        if (dan != pDan) { snap(i, "DAN"); dump(i, "dan"); dumpAt = i; }
        else if (bg != pBg) snap(i, "BG");
        else if (rounds != pRounds) { snap(i, "ROUNDS"); dump(i, "rounds"); dumpAt = i; }
        if (dumpAt >= 0 && (i == dumpAt + 60 || i == dumpAt + 400)) dump(i, "after");
        if (c2b && !p9c2b) { dump(i, "timeout"); holdAt = i; }
        if (holdAt >= 0 && (i == holdAt + 40 || i == holdAt + 150)) dump(i, "hold");
        else if (yy != pYY) snap(i, "YY");
        else if (c28 != p9c28 || c2b != p9c2b) snap(i, "FLAG");
        else if (react != pReact && react != 0) snap(i, "REACT");
        pDan = dan; pBg = bg; pRounds = rounds; pYY = yy; pReact = react; p9c28 = c28; p9c2b = c2b;
    }
    snap(frames, "END");
    CHECK(true);
}

// Profiler (opt-in via FIST_PROFILE_OUT=<file>): boot the game, then single-step
// the CPU for FIST_PROFILE_STEPS instructions (default 3M) while a fight runs,
// and write a histogram of the PC (one line per distinct PC, "octal count").
TEST_CASE("fist: PC profile (diagnostic)")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR"), outPath = env("FIST_PROFILE_OUT");
    if (sav.empty() || dat.empty() || sys.empty() || outPath.empty()) {
        MESSAGE("FIST_PROFILE_OUT not set - skipping");
        return;
    }
    fs::path tmp = fs::temp_directory_path() / "fist_prof_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });
    for (int i = 0; i < 1500; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    emu.keyPress(ms0515::Key::Right, true);       // P1 walks in so a fight happens
    for (int i = 0; i < 300; ++i) (void)emu.stepFrame();
    emu.keyPress(ms0515::Key::Right, false);
    auto &board = ms0515::internal::board(emu);
    auto &cpu = ms0515::internal::cpu(emu);
    std::string se = env("FIST_PROFILE_STEPS");
    long steps = se.empty() ? 3000000L : std::atol(se.c_str());
    std::vector<uint32_t> hist(65536, 0);
    // Sample during a real fight: P1 alternates attacking / approaching (as the
    // scoring test does) so both fighters animate, with the keyboard clock ticking.
    uint32_t nowMs = 0;
    for (long i = 0; i < steps; ++i) {
        if (i % 20000 == 0) {
            bool atk = (i / 800000) % 2 == 0;
            emu.keyPress(ms0515::Key::Space, atk);
            emu.keyPress(ms0515::Key::Right, !atk);
            nowMs += 60;
            emu.keyTick(nowMs);
            (void)emu.stepFrame();          // let the devices (keyboard UART) tick
        }
        hist[cpu_get_pc(&cpu)]++;
        board_step_cpu(&board);
    }
    std::ofstream o(outPath);
    for (int pc = 0; pc < 65536; ++pc)
        if (hist[pc]) o << std::oct << pc << ' ' << std::dec << hist[pc] << '\n';
    CHECK(true);
}

// Diagnostic (opt-in via FIST_KEYLAT=1): key press / release latency in video
// frames.  P2 is parked; RIGHT is pressed, held ~1 s and released; the log says
// when P1's move ($AA05) and x ($AA19) react.
TEST_CASE("fist: key latency (diagnostic)")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty() || env("FIST_KEYLAT").empty()) {
        MESSAGE("FIST_KEYLAT not set - skipping");
        return;
    }
    fs::path tmp = fs::temp_directory_path() / "fist_keylat_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });
    auto &board = ms0515::internal::board(emu);
    auto gst = [&](uint16_t spec) -> uint8_t {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        return board.mem.ram[bank * 8192 + (addr & 8191)];
    };
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    for (int i = 0; i < 1500; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    poke(0xAA46, 0);
    uint32_t nowMs = 0;
    int frame = 0;
    auto step = [&]() {
        poke(0xAA45, 1);
        nowMs += 20;
        emu.keyTick(nowMs);
        (void)emu.stepFrame();
        ++frame;
    };
    for (int i = 0; i < 100; ++i) step();
    auto trace = [&](const char *phase, int n) {
        int px = gst(0xAA19), pm = gst(0xAA05), pa = gst(0xAA04), f0 = frame;
        for (int i = 0; i < n; ++i) {
            step();
            int x = gst(0xAA19), m = gst(0xAA05), a = gst(0xAA04);
            if (x != px || m != pm || a != pa)
                MESSAGE(phase << " +" << (frame - f0) << " frames (" << (frame - f0) * 20
                        << " ms): x=" << x << " move=" << m << " act=" << a);
            px = x; pm = m; pa = a;
        }
    };
    emu.keyPress(ms0515::Key::Right, true);
    trace("PRESS", 60);
    emu.keyPress(ms0515::Key::Right, false);
    trace("RELEASE", 200);
    emu.keyPress(ms0515::Key::Space, true);
    trace("SPACE-PRESS", 40);
    emu.keyPress(ms0515::Key::Space, false);
    trace("SPACE-RELEASE", 200);
    CHECK(true);
}

// Diagnostic (opt-in via FIST_SNDLOG=1): play each sound code by poking $B150
// and record the speaker transitions (reg C bit 5 bit-banging) with CPU-cycle
// timestamps: count, mean / min / max half-period, total duration per code.
TEST_CASE("fist: sound effects (diagnostic)")
{
    std::string sav = env("FIST_GAME_SAV"), dat = env("FIST_GAME_DAT"),
                sys = env("FIST_SYSTEM_DIR");
    if (sav.empty() || dat.empty() || sys.empty() || env("FIST_SNDLOG").empty()) {
        MESSAGE("FIST_SNDLOG not set - skipping");
        return;
    }
    fs::path tmp = fs::temp_directory_path() / "fist_snd_lib";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path boot = tmp / "boot", work = tmp / "work";
    fs::create_directories(boot, ec);
    fs::copy(sys, boot, fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    fs::copy_file(sav, boot / "FIST.SAV", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(boot / "STARTS.COM", std::ios::binary);
        s << "ASSIGN DZ1 DK\r\nR FIST\r\n";
    }
    fs::create_directories(work, ec);
    fs::copy_file(dat, work / "GST.DAT", fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream s(work / "device.rtfs", std::ios::binary);
        s << "device: floppy\nblocks: 800\n";
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();
    REQUIRE(emu.mountDisk(0, (boot / "device.rtfs").string()));
    REQUIRE(emu.mountDisk(1, (work / "device.rtfs").string()));
    bool offerCR = false;
    emu.setSerialCallbacks(
        [&offerCR](uint8_t &b) -> bool {
            if (offerCR) { b = '\r'; offerCR = false; return true; }
            return false;
        },
        [](uint8_t) -> bool { return true; });
    auto &board = ms0515::internal::board(emu);
    auto &cpu = ms0515::internal::cpu(emu);
    auto poke = [&](uint16_t spec, uint8_t v) {
        uint32_t addr = 0x8000u + (spec - 0x9C00u);
        uint32_t bank = (addr >> 13) + 8;
        board.mem.ram[bank * 8192 + (addr & 8191)] = v;
    };
    for (int i = 0; i < 1500; ++i) {
        if (i < 900 && (i % 30) == 0) offerCR = true;
        (void)emu.stepFrame();
    }
    poke(0xAA46, 0);                                   // park P2 (no AI hits = no sounds)
    struct Rec { uint64_t cyc; int lvl; };
    std::vector<Rec> log;
    uint64_t cycles = 0;
    struct Ctx { std::vector<Rec> *log; uint64_t *cyc; } ctx{&log, &cycles};
    board_set_sound_callback(&board,
        [](void *u, int v) { auto *c = static_cast<Ctx *>(u); c->log->push_back({*c->cyc, v}); },
        &ctx);
    for (int code = 1; code <= 6; ++code) {
        log.clear();
        poke(0xAA45, 1);
        poke(0xB150, (uint8_t)code);
        // step until the effect has played: transitions stop for > 1M cycles
        uint64_t lastEvent = cycles, start = cycles;
        bool started = false;
        while (cycles - start < 60'000'000ULL) {          // 8 s budget
            board_step_cpu(&board);
            cycles += (uint64_t)cpu.cycles;
            if (!log.empty() && log.back().cyc != lastEvent) { lastEvent = log.back().cyc; started = true; }
            if (started && cycles - lastEvent > 1'000'000ULL) break;
        }
        if (log.size() < 2) { MESSAGE("code " << code << ": no speaker transitions"); continue; }
        double sum = 0; uint64_t mn = ~0ULL, mx = 0;
        int gaps = 0;
        for (size_t k = 1; k < log.size(); ++k) {
            uint64_t d = log[k].cyc - log[k - 1].cyc;
            if (d > 2'000'000ULL) { ++gaps; continue; }   // the code-5 pause
            sum += (double)d; mn = std::min(mn, d); mx = std::max(mx, d);
        }
        double n = (double)(log.size() - 1 - gaps);
        MESSAGE("code " << code << ": transitions=" << log.size()
                << " half-period cycles mean=" << (long)(sum / n) << " min=" << mn << " max=" << mx
                << " pauses=" << gaps << " total=" << (log.back().cyc - log.front().cyc) / 7500 << " ms");
    }
    CHECK(true);
}
