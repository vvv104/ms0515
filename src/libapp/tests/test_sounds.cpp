/*
 * test_sounds.cpp - the recordings of the drive and the keyboard found
 * in a set's folder: what is named is taken, what is missing or not a
 * WAV is silent.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Sounds.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }

/* A 16-bit mono WAV of `n` samples all `value`, at 8000 Hz. */
void writeWav(const fs::path &file, int n, int16_t value)
{
    std::vector<uint8_t> f;
    f.insert(f.end(), {'R', 'I', 'F', 'F'}); put32(f, 0); f.insert(f.end(), {'W', 'A', 'V', 'E'});
    f.insert(f.end(), {'f', 'm', 't', ' '}); put32(f, 16);
    put16(f, 1); put16(f, 1); put32(f, 8000); put32(f, 16000); put16(f, 2); put16(f, 16);
    f.insert(f.end(), {'d', 'a', 't', 'a'}); put32(f, static_cast<uint32_t>(n * 2));
    for (int i = 0; i < n; ++i) put16(f, static_cast<uint16_t>(value));
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(f.data()), static_cast<std::streamsize>(f.size()));
}

struct SetDir {
    fs::path dir;
    explicit SetDir(const char *name) : dir(fs::path(TESTS_BUILD_DIR) / "temp" / name)
    {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~SetDir() { std::error_code ec; fs::remove_all(dir, ec); }
};

} // namespace

TEST_SUITE("Sounds") {

TEST_CASE("a drive set: the motor's three, the seeks by length and direction, the step pulses") {
    SetDir set("sounds_drive");
    writeWav(set.dir / "motor_start.wav", 10, 1);
    writeWav(set.dir / "motor_loop.wav", 20, 2);
    writeWav(set.dir / "seek_in_1.wav", 3, 3);
    writeWav(set.dir / "seek_in_12.wav", 4, 4);
    writeWav(set.dir / "seek_out_7.wav", 5, 5);
    writeWav(set.dir / "step_out.wav", 6, 6);
    writeWav(set.dir / "seek_in_x.wav", 7, 7);            /* not a length: ignored */
    writeWav(set.dir / "seek_in_0.wav", 7, 7);            /* nor a seek of nothing */
    std::ofstream(set.dir / "seek_in_3.wav") << "not a wav at all";

    auto s = ms0515::app::Sounds::loadDriveDir(set.dir);
    REQUIRE(s);
    CHECK(s->motorStart.samples.size() == 10);
    CHECK(s->motorStart.rate == 8000);
    CHECK(s->motorLoop.samples.size() == 20);
    CHECK(s->motorStop.empty());                          /* missing: silent */
    REQUIRE(s->seekIn.size() == 2);
    CHECK(s->seekIn.at(1).samples[0] == 3);
    CHECK(s->seekIn.at(12).samples[0] == 4);
    REQUIRE(s->seekOut.size() == 1);
    CHECK(s->seekOut.at(7).samples.size() == 5);
    CHECK(s->stepIn.empty());
    CHECK(s->stepOut.samples.size() == 6);

    CHECK_FALSE(ms0515::app::Sounds::loadDriveDir(set.dir / "nowhere"));
}

TEST_CASE("a keyboard set: click and bell") {
    SetDir set("sounds_kbd");
    writeWav(set.dir / "click.wav", 8, 9);
    auto s = ms0515::app::Sounds::loadKeyboardDir(set.dir);
    REQUIRE(s);
    CHECK(s->click.samples.size() == 8);
    CHECK(s->bell.empty());
}

TEST_CASE("sets by name come from assets/sounds under the search roots; an unknown name is null") {
    CHECK_FALSE(ms0515::app::Sounds::loadDrive("no-such-drive-set"));
    CHECK_FALSE(ms0515::app::Sounds::loadKeyboard(""));
    CHECK(ms0515::app::Sounds::driveDir("no-such-drive-set").empty());
    for (const auto &name : ms0515::app::Sounds::driveSets())  /* whatever is installed loads */
        CHECK(ms0515::app::Sounds::loadDrive(name));
}

} // TEST_SUITE
