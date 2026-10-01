#include <doctest/doctest.h>

#include "ZeroRun.hpp"

#include <cstdint>
#include <random>
#include <vector>

using ms0515::run::packZeroRuns;
using ms0515::run::unpackZeroRuns;

TEST_SUITE("ZeroRun") {

TEST_CASE("what is packed comes back byte for byte") {
    std::mt19937 rng(515);

    std::vector<std::vector<uint8_t>> samples;
    samples.push_back({});
    samples.push_back(std::vector<uint8_t>(4096, 0));
    samples.push_back({1});
    samples.push_back({0, 0, 0, 7});
    samples.push_back({7, 0, 0, 0});

    std::vector<uint8_t> noise(5000);
    for (auto &b : noise) b = static_cast<uint8_t>(rng());
    samples.push_back(noise);

    /* Mostly zeros: islands of data with gaps short and long. */
    std::vector<uint8_t> sparse(100000, 0);
    for (int island = 0; island < 40; ++island) {
        const std::size_t at = rng() % (sparse.size() - 300);
        for (std::size_t i = 0; i < 1 + rng() % 200; ++i)
            sparse[at + i] = static_cast<uint8_t>(rng() % 3);   /* zeros inside too */
    }
    samples.push_back(sparse);

    for (const auto &sample : samples) {
        const auto packed = packZeroRuns(sample);
        const auto back = unpackZeroRuns(packed);
        REQUIRE(back.has_value());
        CHECK(*back == sample);
    }

    CHECK(packZeroRuns(std::vector<uint8_t>(4096, 0)).size() == 4);
    CHECK(packZeroRuns(sparse).size() < sparse.size() / 10);
}

TEST_CASE("damaged packing is refused") {
    std::vector<uint8_t> data(1000, 0);
    data[10] = 1;
    data[900] = 2;
    const auto packed = packZeroRuns(data);

    CHECK_FALSE(unpackZeroRuns(std::vector<uint8_t>{1, 2}).has_value());

    auto cut = packed;
    cut.pop_back();
    CHECK_FALSE(unpackZeroRuns(cut).has_value());

    auto beyond = packed;                   /* a stretch past the end */
    beyond[4] = 0xFF; beyond[5] = 0xFF;
    CHECK_FALSE(unpackZeroRuns(beyond).has_value());

    auto shorter = packed;                  /* the whole made smaller  */
    shorter[0] = 100; shorter[1] = 0;
    CHECK_FALSE(unpackZeroRuns(shorter).has_value());
}

}
