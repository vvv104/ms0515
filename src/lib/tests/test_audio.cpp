/*
 * test_audio.cpp - the audio renderer: WAV files in, the speaker and the
 * recordings of the drive and the keyboard out, frame by frame.
 */

#include <doctest/doctest.h>

#include <ms0515/Audio.hpp>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using ms0515::AudioRenderer;
using ms0515::DcBlocker;
using ms0515::DriveSounds;
using ms0515::KeyboardSounds;
using ms0515::Pcm;

namespace {

void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void tag(std::vector<uint8_t> &v, const char *t) { v.insert(v.end(), t, t + 4); }

/* A RIFF/WAVE file: `values` interleaved by channel, 8 or 16 bits. */
std::vector<uint8_t> wav(int rate, int channels, int bits, const std::vector<int> &values)
{
    std::vector<uint8_t> data;
    for (int x : values) {
        if (bits == 8) data.push_back(static_cast<uint8_t>(x));
        else put16(data, static_cast<uint16_t>(x));
    }
    std::vector<uint8_t> f;
    tag(f, "RIFF"); put32(f, 0); tag(f, "WAVE");
    tag(f, "fmt "); put32(f, 16);
    put16(f, 1); put16(f, static_cast<uint16_t>(channels)); put32(f, static_cast<uint32_t>(rate));
    put32(f, static_cast<uint32_t>(rate * channels * bits / 8)); put16(f, static_cast<uint16_t>(channels * bits / 8));
    put16(f, static_cast<uint16_t>(bits));
    tag(f, "LIST"); put32(f, 3); f.push_back(1); f.push_back(2); f.push_back(3); f.push_back(0);   /* a chunk to skip, padded */
    tag(f, "data"); put32(f, static_cast<uint32_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

Pcm flat(int rate, int n, int16_t value)
{
    Pcm p;
    p.rate = rate;
    p.samples.assign(static_cast<std::size_t>(n), value);
    return p;
}

/* 50 000 Hz: a 150 000-cycle frame is 1000 samples, 150 cycles each. */
constexpr int      kRate   = 50000;
constexpr uint32_t kFrame  = 150000;

std::vector<int16_t> frame(AudioRenderer &r)
{
    std::vector<int16_t> out(2000);
    const int n = r.render(out.data(), static_cast<int>(out.size()), kFrame);
    out.resize(static_cast<std::size_t>(n));
    return out;
}

bool all(const std::vector<int16_t> &v, std::size_t from, std::size_t to, int16_t value)
{
    for (std::size_t i = from; i < to && i < v.size(); ++i) if (v[i] != value) return false;
    return to <= v.size();
}

} // namespace

TEST_SUITE("audio") {

TEST_CASE("parseWav: 16-bit mono as is, stereo averaged, 8-bit widened, other chunks skipped") {
    auto f = wav(8000, 1, 16, {100, -200, 300});
    auto p = ms0515::parseWav(f.data(), f.size());
    REQUIRE(p);
    CHECK(p->rate == 8000);
    CHECK(p->samples == std::vector<int16_t>{100, -200, 300});

    f = wav(22050, 2, 16, {100, 300, -200, -400});
    p = ms0515::parseWav(f.data(), f.size());
    REQUIRE(p);
    CHECK(p->samples == std::vector<int16_t>{200, -300});

    f = wav(11025, 1, 8, {128, 255, 0});
    p = ms0515::parseWav(f.data(), f.size());
    REQUIRE(p);
    CHECK(p->samples == std::vector<int16_t>{0, 127 << 8, -128 << 8});

    CHECK_FALSE(ms0515::parseWav(nullptr, 0));
    const char junk[] = "RIFFxxxxWAVEfmt ";
    CHECK_FALSE(ms0515::parseWav(reinterpret_cast<const uint8_t *>(junk), sizeof junk));
    f = wav(8000, 1, 16, {1, 2, 3, 4});
    f.resize(f.size() - 3);                              /* cut mid-file: what is there is taken */
    p = ms0515::parseWav(f.data(), f.size());
    REQUIRE(p);
    CHECK(p->samples.size() == 2);
}

TEST_CASE("the speaker: its level at each sample from the frame's transitions, carried between frames") {
    AudioRenderer r(kRate);
    r.speaker(0, 1);
    r.speaker(kFrame / 2, 0);
    auto out = frame(r);
    REQUIRE(out.size() == 1000);
    CHECK(all(out, 0, 500, AudioRenderer::kSpeakerAmplitude));
    CHECK(all(out, 500, 1000, -AudioRenderer::kSpeakerAmplitude));
    out = frame(r);
    CHECK(all(out, 0, 1000, -AudioRenderer::kSpeakerAmplitude));   /* the level stays */
    r.setSpeakerVolume(0.0f);
    CHECK(all(frame(r), 0, 1000, 0));
}

TEST_CASE("the motor: start, then the loop for as long as it runs, then stop") {
    auto d = std::make_shared<DriveSounds>();
    d->motorStart = flat(kRate, 100, 1000);
    d->motorLoop  = flat(kRate, 100, 2000);
    d->motorStop  = flat(kRate, 50, 3000);
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setDriveSounds(d);

    r.motor(0, 0, true);
    r.motor(0, 0, true);                                 /* said twice: one motor */
    auto out = frame(r);
    CHECK(all(out, 0, 100, 1000));                       /* the start */
    CHECK(all(out, 100, 1000, 2000));                    /* the loop, seamlessly */
    CHECK(r.motorRunning(0));
    CHECK_FALSE(r.motorRunning(1));

    CHECK(all(frame(r), 0, 1000, 2000));                 /* still running, nothing said */

    r.motor(kFrame / 2, 0, false);
    out = frame(r);
    CHECK(all(out, 0, 500, 2000));
    CHECK(all(out, 500, 550, 3000));                     /* the stop */
    CHECK(all(out, 550, 1000, 0));
    CHECK_FALSE(r.motorRunning(0));
    CHECK(all(frame(r), 0, 1000, 0));
    CHECK(r.voices() == 0);

    r.motor(0, 1, true);
    CHECK(all(frame(r), 100, 1000, 2000));
    r.reset();
    CHECK_FALSE(r.motorRunning(1));
    CHECK(all(frame(r), 0, 1000, 0));
}

TEST_CASE("a seek: the recording of its length, else the nearest, else a pulse per track at the step rate") {
    auto d = std::make_shared<DriveSounds>();
    d->seekIn[5]  = flat(kRate, 20, 500);
    d->seekIn[10] = flat(kRate, 20, 700);
    d->stepOut    = flat(kRate, 10, 900);
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setDriveSounds(d);

    r.seek(kFrame / 2, 5, 3000);
    auto out = frame(r);
    CHECK(all(out, 0, 500, 0));
    CHECK(all(out, 500, 520, 500));
    CHECK(all(out, 520, 1000, 0));

    r.seek(0, 7, 3000);                                  /* 7: nearer to 5 than to 10 */
    CHECK(all(frame(r), 0, 20, 500));
    r.seek(0, 9, 3000);
    CHECK(all(frame(r), 0, 20, 700));
    r.seek(0, 79, 3000);                                 /* beyond the last recorded: the last */
    CHECK(all(frame(r), 0, 20, 700));

    r.seek(0, -3, 3000);                                 /* out: no seeks recorded, pulses 3000 cycles = 20 samples apart */
    out = frame(r);
    CHECK(all(out, 0, 10, 900));
    CHECK(all(out, 10, 20, 0));
    CHECK(all(out, 20, 30, 900));
    CHECK(all(out, 40, 50, 900));
    CHECK(all(out, 50, 1000, 0));
}

TEST_CASE("the keyboard: clicks overlap, the bell needs a recording, the volume scales") {
    auto k = std::make_shared<KeyboardSounds>();
    k->click = flat(kRate, 10, 400);
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setKeyboardSounds(k);
    /* One element, one firmware: a second click waits for the first to
     * end rather than doubling on top of it. */
    r.keyClick(0);
    r.keyClick(0);
    r.bell(0);
    auto out = frame(r);
    CHECK(all(out, 0, 10, 400));
    CHECK(all(out, 10, 20, 400));
    CHECK(all(out, 20, 1000, 0));                        /* and the bell has no recording here */
    r.setKeyboardVolume(0.5f);
    r.keyClick(kFrame - 150);                            /* the last sample of the frame ... */
    out = frame(r);
    CHECK(out[999] == 200);
    CHECK(all(frame(r), 0, 9, 200));                     /* ... and on into the next */
}

TEST_CASE("a recording at another rate is resampled, and one due after the frame waits for it") {
    auto k = std::make_shared<KeyboardSounds>();
    k->click.rate = kRate / 2;
    k->click.samples = {0, 1000, 2000, 3000};
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setKeyboardSounds(k);
    r.keyClick(0);
    auto out = frame(r);
    CHECK(out[0] == 0);
    CHECK(out[1] == 500);
    CHECK(out[2] == 1000);
    CHECK(out[5] == 2500);
    CHECK(out[6] == 3000);
    CHECK(out[8] == 0);

    auto d = std::make_shared<DriveSounds>();
    d->stepIn = flat(kRate, 10, 900);
    r.setDriveSounds(d);
    r.seek(kFrame - 150, 3, 150000);                     /* pulses a whole frame apart: one per frame */
    out = frame(r);
    CHECK(out[999] == 900);
    out = frame(r);
    CHECK(all(out, 0, 9, 900));
    CHECK(all(out, 999, 1000, 900));
    out = frame(r);
    CHECK(all(out, 999, 1000, 900));
    CHECK(all(frame(r), 0, 9, 900));
}

} // TEST_SUITE

/* ---- the machine's events through the Emulator ------------------------------ */

#include <ms0515/Emulator.hpp>
#include "test_disk.hpp"

TEST_CASE("audio: the Emulator reports the boot's motor and seeks with their cycles, and a key's click") {
    ms0515_test::TempDisk disk(std::string{TESTS_DIR} + "/disks/test_osa.dsk");
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
    REQUIRE(emu.mountDisk(0, disk.path().string()));
    std::vector<ms0515::MechEvent> events;
    emu.setMechCallback([&](const ms0515::MechEvent &e) { events.push_back(e); });
    emu.reset();
    for (int f = 0; f < 1000; ++f) (void)emu.stepFrame();   /* 20 s: the startup file has run its course */

    int motors = 0, seeks = 0;
    for (const auto &e : events) {
        if (e.kind == ms0515::MechEvent::Kind::motorOn) ++motors;
        if (e.kind == ms0515::MechEvent::Kind::seek) { ++seeks; CHECK(e.stepCycles > 0); CHECK(e.arg != 0); }
        CHECK(e.cycle < 200000);                          /* within a frame */
    }
    /* And it stops again: the guest clears the motor bit once it has read
     * what it wanted, so the sound must not run on for ever. */
    int running = 0;
    for (const auto &e : events) {
        if (e.kind == ms0515::MechEvent::Kind::motorOn) ++running;
        if (e.kind == ms0515::MechEvent::Kind::motorOff) --running;
    }
    CHECK(running == 0);
    CHECK(motors >= 1);                                   /* the boot spins the drive ... */
    CHECK(seeks >= 1);                                    /* ... and moves the head */

    events.clear();
    emu.reset();                                          /* the keyboard is not power-cycled with the machine */
    for (const auto &e : events) CHECK(e.kind != ms0515::MechEvent::Kind::bell);
}

TEST_CASE("audio: the MS7004's own sounds from its firmware - a tick of ~2 ms, a bell of 1.9 kHz for 66 ms") {
    const auto k = ms0515::ms7004KeyboardSounds(48000);
    CHECK(k.click.rate == 48000);
    /* Short enough to be a tick, and to clear the 30 ms the typematic
     * leaves between repeats rather than smear into the next one. */
    CHECK(k.click.samples.size() >= 80);
    CHECK(k.click.samples.size() <= 120);
    int peak = 0, zeroCrossings = 0;
    for (std::size_t i = 0; i < k.click.samples.size(); ++i) {
        const int v = k.click.samples[i];
        peak = std::max(peak, v < 0 ? -v : v);
        if (i && ((v < 0) != (k.click.samples[i - 1] < 0))) ++zeroCrossings;
    }
    CHECK(peak > 8000);                                   /* it carries */
    CHECK(zeroCrossings >= 8);                            /* and it rings, not thuds */
    /* 254 half-periods of 80 cycles = 66 ms, plus the ring dying away. */
    CHECK(k.bell.samples.size() >= 3300);
    CHECK(k.bell.samples.size() <= 3500);
}

TEST_CASE("audio: the beeper's peak follows the volume, boot melody included") {
    ms0515_test::TempDisk disk(std::string{TESTS_DIR} + "/disks/test_osa.dsk");
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
    REQUIRE(emu.mountDisk(0, disk.path().string()));
    AudioRenderer r(44100);
    emu.setSoundCallback([&](int v) { r.speaker(emu.frameCyclePos(), v); });
    emu.reset();
    auto peakOverBoot = [&](float gain) {
        r.setSpeakerVolume(gain);
        std::vector<int16_t> out(4410);
        int peak = 0;
        for (int f = 0; f < 200; ++f) {
            (void)emu.stepFrame();
            const int n = r.render(out.data(), static_cast<int>(out.size()),
                                   static_cast<uint32_t>(emu.frameCyclePos()));
            for (int i = 0; i < n; ++i) { const int a = out[i] < 0 ? -out[i] : out[i]; if (a > peak) peak = a; }
        }
        return peak;
    };
    const int full = peakOverBoot(1.0f);
    const int half = peakOverBoot(0.5f);
    CHECK(full == AudioRenderer::kSpeakerAmplitude);      /* the square wave's own level */
    CHECK(half == AudioRenderer::kSpeakerAmplitude / 2);  /* and the volume really scales it */
}

TEST_CASE("audio: a key held with auto-repeat on clicks, as the keyboard does; with it off, silence") {
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
    int clicks = 0;
    emu.setMechCallback([&](const ms0515::MechEvent &e) {
        if (e.kind == ms0515::MechEvent::Kind::keyClick) ++clicks;
    });
    emu.reset();

    /* The typematic runs on the machine's own clock: stepping frames is
     * all it takes. */
    auto hold = [&](int frames) {
        emu.keyPress(ms0515::Key::A, true);
        for (int f = 0; f < frames; ++f) (void)emu.stepFrame();
        emu.keyPress(ms0515::Key::A, false);
    };

    auto settings = emu.keyboardSettings();
    CHECK(settings.repeatEnabled);                    /* on at power-on, as the firmware has it */
    hold(50);
    CHECK(clicks > 5);                                /* 250 ms delay, then one every 30 ms */

    settings.repeatEnabled = false;                   /* switched off: the keyboard goes quiet */
    emu.applyKeyboardConfig(settings);
    clicks = 0;
    hold(50);
    CHECK(clicks == 0);
}

TEST_CASE("audio: an event between two renders is not lost") {
    auto k = std::make_shared<KeyboardSounds>();
    k->click = flat(kRate, 10, 400);
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setKeyboardSounds(k);

    (void)frame(r);                                   /* a frame goes by ... */
    r.keyClick(kFrame - 150);                         /* ... an event lands after it ... */
    auto out = frame(r);                              /* ... and the next render has it */
    CHECK(out[999] == 400);
}

TEST_CASE("the loudspeaker's high pass: the offset goes, the edges stay") {
    DcBlocker cone(kRate);
    std::vector<int16_t> held(kRate, -1500);          /* a second of a held level */
    cone.apply(held.data(), static_cast<int>(held.size()));
    CHECK(held[0] == -1500);                          /* the step itself is kept */
    CHECK(std::abs(held[kRate / 100]) < 1000);        /* 10 ms on, most of it gone */
    CHECK(std::abs(held[kRate / 10]) < 10);           /* 100 ms on, nothing left */

    DcBlocker again(kRate);
    std::vector<int16_t> square(kRate / 10);          /* a 1 kHz beep */
    for (std::size_t i = 0; i < square.size(); ++i)
        square[i] = (i / (kRate / 2000)) % 2 == 0 ? 1500 : -1500;
    std::vector<int16_t> filtered = square;
    again.apply(filtered.data(), static_cast<int>(filtered.size()));
    double a = 0.0, b = 0.0;
    for (std::size_t i = 0; i < square.size(); ++i) {
        a += static_cast<double>(square[i]) * square[i];
        b += static_cast<double>(filtered[i]) * filtered[i];
    }
    CHECK(b / a > 0.99);                              /* the beep is untouched */
    CHECK(b / a < 1.05);
}

TEST_CASE("a move takes the recording that ran nearest the same way") {
    auto d = std::make_shared<DriveSounds>();
    /* Three moves, told apart by what they carry. */
    d->moves.push_back({0, 40, flat(kRate, 30, 100)});     /* out of track 0 */
    d->moves.push_back({40, 79, flat(kRate, 30, 200)});    /* on from there */
    d->moves.push_back({79, 0, flat(kRate, 30, 300)});     /* and home */
    AudioRenderer r(kRate);
    r.setSpeakerVolume(0.0f);
    r.setDriveSounds(d);

    /* From track 0, forty tracks in: the first of them. */
    r.seek(0, 40, 3000);
    CHECK(all(frame(r), 0, 30, 100));
    for (int i = 0; i < 3; ++i) (void)frame(r);

    /* On from track 40 to the last: the second, though both are about forty
     * tracks long - the head is somewhere else now, and it sounds different. */
    r.seek(0, 39, 3000);
    CHECK(all(frame(r), 0, 30, 200));
    for (int i = 0; i < 3; ++i) (void)frame(r);

    /* And all the way home. */
    r.seek(0, -79, 3000);
    CHECK(all(frame(r), 0, 30, 300));
}
