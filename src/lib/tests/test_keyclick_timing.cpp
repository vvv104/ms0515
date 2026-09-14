/*
 * test_keyclick_timing.cpp - the keyboard's clicks, timed in the rendered
 * stream the way a front-end plays it.
 *
 * The typematic must run on the machine's clock, not the host's: when it
 * ran on the host's frame the repeats landed on whatever grid the host
 * happened to have, and the clicks came out 40, 20, 40, 40, 20 ms apart -
 * a rattle that did not match the letters appearing on the screen.
 */
#include <doctest/doctest.h>
#include <ms0515/Emulator.hpp>
#include <ms0515/Audio.hpp>
#include "test_disk.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("a held key clicks evenly, at the typematic's own period") {
    ms0515_test::TempDisk disk(std::string{TESTS_DIR} + "/disks/test_osa.dsk");
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
    REQUIRE(emu.mountDisk(0, disk.path().string()));
    const int rate = 44100;
    ms0515::AudioRenderer r(rate);
    r.setKeyboardSounds(std::make_shared<ms0515::KeyboardSounds>(ms0515::ms7004KeyboardSounds(rate)));
    r.setSpeakerVolume(0.0f);                       /* the keyboard alone, to see it clean */
    std::vector<double> madeAtMs;                   /* when the model made a click */
    double emuMs = 0;
    emu.setMechCallback([&](const ms0515::MechEvent &e) {
        if (e.kind == ms0515::MechEvent::Kind::keyClick) { r.keyClick(e.cycle); madeAtMs.push_back(emuMs); }
    });
    emu.reset();

    std::vector<int16_t> pcm, buf(4410);
    /* The front-end's loop: host frames of 16.7 ms, emulated frames of
     * 20 ms taken from the accumulated real time. */
    double accum = 0;
    bool held = false;
    for (int hostFrame = 0; hostFrame < 400; ++hostFrame) {
        accum += 16.667;
        while (accum >= 20.0) {
            (void)emu.stepFrame();
            const int n = r.render(buf.data(), (int)buf.size(), emu.frameCyclePos());
            pcm.insert(pcm.end(), buf.begin(), buf.begin() + n);
            accum -= 20.0;
            emuMs += 20.0;
        }
        if (!held && hostFrame == 300) { emu.keyPress(ms0515::Key::A, true); held = true; }
    }
    emu.keyPress(ms0515::Key::A, false);

    /* Click onsets in the recording. */
    std::vector<double> heardAtMs;
    for (std::size_t i = 1; i < pcm.size(); ++i) {
        const int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        const int b = pcm[i-1] < 0 ? -pcm[i-1] : pcm[i-1];
        if (a > 3000 && b <= 3000) {
            const double ms = 1000.0 * (double)i / rate;
            if (heardAtMs.empty() || ms - heardAtMs.back() > 5.0) heardAtMs.push_back(ms);
        }
    }
    REQUIRE(madeAtMs.size() > 10);
    CHECK(heardAtMs.size() == madeAtMs.size());        /* every click the model made is in the stream */
    const double period = emu.keyboardSettings().typingPeriodMs;
    for (std::size_t i = 1; i < heardAtMs.size(); ++i) {
        const double gap = heardAtMs[i] - heardAtMs[i - 1];
        CHECK(gap > period - 2.0);                     /* even, at the typematic's period */
        CHECK(gap < period + 2.0);
    }
}
