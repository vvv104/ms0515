/*
 * test_stars.cpp - the flight, seen from outside.
 */

#include "Flight.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

using namespace stars;

namespace {

/* The machine is deterministic and the program seeds nothing, so two
 * flights with the same keys draw the same frames: a difference between
 * two flights is the keys' doing and nothing else. */
constexpr int kWarmUp = 60;      /* frames before the first key: the field is up */
constexpr int kFlight = 80;      /* frames the keys are held */

std::vector<uint8_t> flyWith(ms0515::Key key)
{
    Flight flight;
    flight.run(kWarmUp);
    for (int f = 0; f < kFlight; f += 3) {
        if (key != ms0515::Key::None) flight.tap(key);
        flight.run(3);
    }
    return flight.pixels();
}

}  // namespace

TEST_SUITE("STARS") {

TEST_CASE("the screen goes to 320x200 colour, black with bright white stars that move") {
    if (!built()) { MESSAGE("STARS.SAV not built - skipped"); return; }
    Flight flight;
    flight.run(kWarmUp);
    REQUIRE_FALSE(flight.machine.ended());
    CHECK(flight.machine.graphics());
    CHECK_FALSE(flight.machine.emulator().isHires());

    const auto attributes = flight.attributes();
    CHECK(std::count(attributes.begin(), attributes.end(), 0x47) == static_cast<long>(attributes.size()));

    const int lit = flight.lit();
    CHECK(lit > 15);
    CHECK(lit <= 200);

    const auto before = flight.pixels();
    flight.run(10);
    CHECK(flight.pixels() != before);
}

/* The program says on its way out how many passes of its main loop it
 * made in how many frames. */
std::pair<unsigned, unsigned> passesAndFrames(Flight &flight)
{
    flight.tap('q');
    flight.run(200);
    REQUIRE(flight.machine.ended());
    unsigned passes = 0, frames = 0;
    const std::string printed = flight.printed();
    MESSAGE(printed);
    std::istringstream line(printed);
    std::string word, in, passesWord;
    REQUIRE((line >> word >> passes >> passesWord >> in >> frames));
    REQUIRE(word == "STARS:");
    return {passes, frames};
}

/* A pass of the main loop is paced at two frames, 25 a second, and must
 * fit in them, or the flight slows and the keys lag: all but a few
 * passes, straight on and turning alike. */
TEST_CASE("the main loop keeps its pace: a pass every two frames") {
    if (!built()) { MESSAGE("STARS.SAV not built - skipped"); return; }
    Flight flight;
    flight.run(200);
    const auto [passes, frames] = passesAndFrames(flight);
    CHECK(frames >= 150);
    CHECK(passes * 2 <= frames);
    CHECK(passes * 20 >= frames * 9);
}

TEST_CASE("the main loop keeps its pace while turning") {
    if (!built()) { MESSAGE("STARS.SAV not built - skipped"); return; }
    Flight flight;
    flight.run(kWarmUp);
    for (int f = 0; f < 150; f += 3) {
        flight.tap(ms0515::Key::Left);
        flight.run(3);
    }
    const auto [passes, frames] = passesAndFrames(flight);
    CHECK(frames >= 150);
    CHECK(passes * 20 >= frames * 9);
}

TEST_CASE("the arrows change the flight, and the same keys fly the same flight") {
    if (!built()) { MESSAGE("STARS.SAV not built - skipped"); return; }
    const auto straight = flyWith(ms0515::Key::None);
    CHECK(flyWith(ms0515::Key::None) == straight);
    CHECK(flyWith(ms0515::Key::Left) != straight);
    CHECK(flyWith(ms0515::Key::Right) != straight);
    CHECK(flyWith(ms0515::Key::Up) != straight);
    CHECK(flyWith(ms0515::Key::Down) != straight);
    CHECK(flyWith(ms0515::Key::Left) != flyWith(ms0515::Key::Right));
}

TEST_CASE("Q ends the flight and the console comes back in its own mode") {
    if (!built()) { MESSAGE("STARS.SAV not built - skipped"); return; }
    Flight flight;
    flight.run(kWarmUp);
    flight.tap('q');
    flight.run(200);
    REQUIRE(flight.machine.ended());
    CHECK_FALSE(flight.machine.failed());
    CHECK(flight.machine.emulator().isHires());
}

}
