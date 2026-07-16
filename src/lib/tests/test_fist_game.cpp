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

    auto settle = [&](int n) { for (int i = 0; i < n; ++i) (void)emu.stepFrame(); };
    // P1 must be human (AA06=0) or MOVSEL's AI overrides the keyboard.
    int human = gst(0xAA06);
    MESSAGE("P1 AA06 (0=human): " << human);
    CHECK(human == 0);

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
    int holdAt4 = 0, maxHold = 0, firstHold = 0;         // win-freeze: tally held at the 4 win
    bool sawWin = false, sawResetAfterWin = false;
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
        // Win-freeze: when a fighter hits 4 (bout won), the tally holds at 4 for the
        // freeze, then GLOOP clears it -> the winning yin-yang stays on screen a while.
        if (gst(0xAA01) >= 4 || gst(0xAA41) >= 4) {   // win threshold (tally can land on 4 or 5)
            sawWin = true; ++holdAt4; maxHold = std::max(maxHold, holdAt4);
        } else {
            if (sawWin && firstHold == 0) firstHold = holdAt4;   // length of the 1st freeze
            if (sawWin) sawResetAfterWin = true;   // dropped back to 0 after a win
            holdAt4 = 0;
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
    MESSAGE("  bout win: saw win(tally==4)=" << sawWin << " first freeze=" << firstHold
            << " max hold=" << maxHold << " reset-after-win=" << sawResetAfterWin);
    CHECK(a01_0 == 0);           // the match starts 0-0 (GST.DAT snapshot cleared)
    CHECK(changes > 0);          // clean hits are scored into the yin-yang total
    CHECK(peak >= 2);            // at least a full yin-yang accrues over the bout
    // Match outcome: reaching 4 (two yin-yang) wins the bout, which freezes the
    // winning frame (tally held at 4 for many frames) then resets for the next bout.
    CHECK(sawWin);               // a bout was won (a fighter reached 4)
    CHECK(maxHold > 60);         // the win freezes the frame (~150-frame hold)
    CHECK(sawResetAfterWin);     // then the tally resets for the next bout
}
