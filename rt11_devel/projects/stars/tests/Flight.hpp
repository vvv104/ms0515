/*
 * Flight.hpp - STARS under test.
 *
 * STARS.SAV run the way `ms0515-run STARS` runs it - the machine of
 * ms0515-run, without a terminal or a window - the frames counted, the
 * keys pressed, the picture read out of video memory: 200 rows of 40
 * words, the pixel byte under the attribute byte.  The folder is the
 * project's build/sav unless --stars-sav=<folder> names another; the
 * suite skips itself when there is none.
 */
#pragma once

#include <doctest/doctest.h>

#include "Machine.hpp"
#include "EmulatorInternal.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/Typist.hpp>

extern "C" {
#include <ms0515/core/board.h>
}

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace stars {

namespace fs = std::filesystem;

inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> opts;
    return opts;
}

inline fs::path savDir()
{
    auto it = options().find("sav");
    return it == options().end() || it->second.empty() ? fs::path(STARS_SAV_DIR) : fs::path(it->second);
}

inline bool built() { return fs::exists(savDir() / "STARS.SAV"); }

constexpr int kRows = 200;
constexpr int kStride = 80;

class Flight {
public:
    ms0515::run::Machine machine;

    /* STARS started from a copy of its folder. */
    Flight()
    {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("ms0515_stars_" + std::to_string(counter++));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        fs::copy_file(savDir() / "STARS.SAV", dir_ / "STARS.SAV");
        REQUIRE(machine.start(dir_ / "STARS.SAV", {}));
    }
    ~Flight()
    {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    Flight(const Flight &)            = delete;
    Flight &operator=(const Flight &) = delete;

    /* So many frames of the machine, the keys queued pressed as they go. */
    void run(int frames)
    {
        for (int f = 0; f < frames && !machine.ended(); ++f) {
            typist_.pump(machine.emulator());
            machine.step();
        }
    }
    /* A key tapped: pressed, held, let go - as a person's press would be.
     * Tapped again every few frames it is a key held down to the program,
     * which hears a code and then its repeats. */
    void tap(ms0515::Key key) { typist_.type(key); }
    void tap(char c)          { typist_.type(static_cast<uint8_t>(c)); }

    /* The pixel bytes of the screen: 200 rows of 40. */
    std::vector<uint8_t> pixels()
    {
        const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
        std::vector<uint8_t> out;
        for (int row = 0; row < kRows; ++row)
            for (int cell = 0; cell < kStride; cell += 2)
                out.push_back(vram[row * kStride + cell]);
        return out;
    }
    /* The attribute bytes, the same way. */
    std::vector<uint8_t> attributes()
    {
        const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
        std::vector<uint8_t> out;
        for (int row = 0; row < kRows; ++row)
            for (int cell = 0; cell < kStride; cell += 2)
                out.push_back(vram[row * kStride + cell + 1]);
        return out;
    }
    /* How many pixels are lit. */
    int lit()
    {
        int n = 0;
        for (uint8_t b : pixels())
            for (int bit = 0; bit < 8; ++bit)
                n += (b >> bit) & 1;
        return n;
    }

private:
    fs::path dir_;
    ms0515::Typist typist_;
};

}  // namespace stars
