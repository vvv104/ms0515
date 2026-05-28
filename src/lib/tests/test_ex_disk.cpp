/*
 * test_ex_disk.cpp — high-level regression for the EX RAM-disk driver.
 *
 * Boots assets/disks/disk2.dsk (RT-11 + EX.SYS) under ROM-B, drives
 * past the date/time/startup-command prompts with three Enters, then
 * runs `INIT EX:` / `Y` / `DIR EX:` and verifies the directory comes
 * back as an empty RT-11 volume.  Catches regressions in the T-11
 * DATIO bus-cycle modelling — without DATIO atomicity the K555IE19
 * counter on the EX board double-ticks every MOVB and `DIR EX:`
 * surfaces `?DIR-F-Invalid directory`.
 *
 * Test fixture lives in `src/assets/disks/` instead of the usual
 * `lib/tests/disks/` tree: disk2.dsk is the shipped EX-capable image
 * end-users get, the test rides on top of it through a TempDisk copy
 * so the original stays pristine.
 */

#include <doctest/doctest.h>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/VramMirror.hpp>
#include "EmulatorInternal.hpp"

#include "test_disk.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

namespace {

constexpr const char *kRomB  = ASSETS_DIR "/rom/ms0515-romb.rom";
constexpr const char *kDisk  = ASSETS_DIR "/disks/disk2.dsk";

static void stepFrames(ms0515::Emulator &emu, ms0515::VramMirror &mirror, int n)
{
    for (int i = 0; i < n; ++i) {
        (void)emu.stepFrame();
        mirror.flushFrame();
    }
}

static void tap(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, mirror, 2);
    emu.keyPress(key, false);
    stepFrames(emu, mirror, 8);
}

/* Spin frames until VramMirror has been quiet for `quiet` consecutive
 * frames, or `cap` frames elapsed.  Mirrors how the CLI gates input. */
static void waitForIdle(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                        int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap; ++i) {
        const size_t before = mirror.history().size();
        (void)emu.stepFrame();
        mirror.flushFrame();
        if (mirror.history().size() == before) ++q;
        else                                   q = 0;
        if (q >= quiet) return;
    }
}

/* Tap each character of `s` as a separate key.  Only the small set
 * of chars used by INIT EX: / DIR EX: / Y is mapped — everything
 * else is silently skipped. */
static void typeString(ms0515::Emulator &emu, ms0515::VramMirror &mirror,
                       const char *s)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G,
        K::H, K::I, K::J, K::K, K::L, K::M, K::N,
        K::O, K::P, K::Q, K::R, K::S, K::T, K::U,
        K::V, K::W, K::X, K::Y, K::Z,
    };
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        K k = K::None;
        if (c >= 'A' && c <= 'Z') k = letters[c - 'A'];
        else if (c == ' ')        k = K::Space;
        else if (c == ':')        k = K::ColonStar;
        else if (c == '\r')       k = K::Return;
        if (k != K::None) tap(emu, mirror, k);
    }
}

/* Pick up the screen as a single string with rows joined by '\n'
 * and trailing blanks trimmed.  Used for substring assertions —
 * the OS's Cyrillic banner is irrelevant, we only check that the
 * specific English DIR output is present. */
static std::string screenAsText(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::string out;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        auto row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        out += row;
        out += '\n';
    }
    return out;
}

}  /* namespace */

TEST_SUITE("ExDisk") {

TEST_CASE("INIT EX: / DIR EX: round-trip through the K555IE19 counter") {
    REQUIRE(fs::exists(kRomB));

    /* The disk2.dsk fixture is an EX-capable RT-11 image; without it
     * this end-to-end check has nothing to drive.  The unit-level
     * DATIO tests in test_board.cpp still cover the bus-cycle
     * mechanics — those run on every build. */
    if (!fs::exists(kDisk)) {
        MESSAGE("disk2.dsk not present in assets/disks/ — skipping the "
                "end-to-end EX RAM-disk regression.  Drop a bootable "
                "RT-11+EX image at " << std::string{kDisk}
                << " to enable it.");
        return;
    }

    /* TempDisk-copy so the OS's INIT writes don't touch the shipped
     * image — same pattern as the rest of the suite. */
    ms0515_test::TempDisk td{kDisk};
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(kRomB));
    const auto pathStr = td.path().string();
    REQUIRE(emu.mountDisk(0, pathStr));
    REQUIRE(emu.mountDisk(2, pathStr));   /* double-sided */
    emu.enableRamDisk();

    ms0515::VramMirror mirror;
    mirror.attach(emu);
    mirror.setOutput(nullptr);
    emu.reset();

    /* Boot to the date prompt. */
    waitForIdle(emu, mirror, /*quiet=*/120, /*cap=*/4000);

    /* Date / time / startup-command — Enter on each accepts the
     * default. */
    tap(emu, mirror, ms0515::Key::Return);
    waitForIdle(emu, mirror, 80, 1500);
    tap(emu, mirror, ms0515::Key::Return);
    waitForIdle(emu, mirror, 80, 1500);
    tap(emu, mirror, ms0515::Key::Return);
    waitForIdle(emu, mirror, 120, 3000);

    /* Initialise the EX device.  The OS interleaves its
     * `EX0:/Initialize; Are you sure?` prompt with the user's input
     * echo (typeahead-style), so we just type the whole INIT-confirm
     * sequence and let the OS settle before checking the result. */
    typeString(emu, mirror, "INIT EX:\rY\r");
    waitForIdle(emu, mirror, 200, 10000);

    /* Read it back — the directory must parse as an empty RT-11
     * volume.  Without DATIO atomicity, half the bytes EX.SYS wrote
     * land on the wrong DRAM offset and the OS prints
     * `?DIR-F-Invalid directory`. */
    typeString(emu, mirror, "DIR EX:\r");
    waitForIdle(emu, mirror, 120, 4000);

    const auto screen = screenAsText(emu);
    INFO("screen after DIR EX::\n" << screen);
    CHECK_FALSE_MESSAGE(screen.find("Invalid directory") != std::string::npos,
        "DIR EX: surfaced ?DIR-F-Invalid directory — DATIO atomicity broken");
    CHECK_MESSAGE(screen.find("0 Files, 0 Blocks") != std::string::npos,
        "DIR EX: did not report an empty RT-11 directory");
    CHECK_MESSAGE(screen.find("1008 Free blocks") != std::string::npos,
        "DIR EX: did not report the expected 1008 free blocks");
}

}  /* TEST_SUITE */
