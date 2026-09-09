/*
 * test_screen.cpp — contract tests for the shared frame composer and the
 * PNG screenshot writer.  A default-constructed Emulator is enough: we
 * assert on the framebuffer's shape and on the file the writer produces,
 * not on any particular guest picture.
 */

#include <doctest/doctest.h>

#include <ms0515/app/Screen.hpp>
#include <ms0515/Emulator.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs  = std::filesystem;
namespace app = ms0515::app;

namespace {

std::vector<unsigned char> readAll(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} /* anonymous namespace */

TEST_CASE("the dim half of the palette carries half the light, not half the code")
{
    /* Attribute bit 14 clear is "half brightness" (docs/hardware/video.md).
     * Halving the code byte does not halve the light: 0x80 on an sRGB
     * display is about a fifth of white, which is what made the machine's
     * dark blue and dark red look black next to every other emulator. */
    const uint32_t red    = app::Screen::paletteColor(0b010, /*bright=*/false);
    const uint32_t bright = app::Screen::paletteColor(0b010, /*bright=*/true);
    CHECK((red    & 0xFFu) == 0xCDu);
    CHECK((bright & 0xFFu) == 0xFFu);

    /* Black stays black in both halves, and a colour lights only its own
     * channel: RGBA is R in bits 0-7, G in 8-15, B in 16-23. */
    CHECK(app::Screen::paletteColor(0b000, false) == 0xFF000000u);
    CHECK(app::Screen::paletteColor(0b000, true)  == 0xFF000000u);
    CHECK(app::Screen::paletteColor(0b100, false) == 0xFF00CD00u);
    CHECK(app::Screen::paletteColor(0b001, false) == 0xFFCD0000u);
    CHECK(app::Screen::paletteColor(0b111, false) == 0xFFCDCDCDu);
}

TEST_CASE("Screen renders an opaque 640x400 frame")
{
    ms0515::Emulator emu;
    app::Screen screen;
    screen.render(emu, /*frameCounter=*/0);

    CHECK(app::Screen::width()  == 640);
    CHECK(app::Screen::height() == 400);

    const uint32_t *px = screen.pixels();
    REQUIRE(px != nullptr);

    /* Every pixel must carry a full alpha byte, else an SDL texture (or
     * a PNG viewer) shows a transparent screen. */
    bool allOpaque = true;
    for (int i = 0; i < app::kScreenWidth * app::kScreenHeight; ++i) {
        if ((px[i] >> 24) != 0xFFu) { allOpaque = false; break; }
    }
    CHECK(allOpaque);
}

TEST_CASE("Screen doubles scanlines in both video modes")
{
    ms0515::Emulator emu;
    app::Screen screen;
    screen.render(emu, 0);
    const uint32_t *px = screen.pixels();

    /* Both modes paint 200 logical rows into 400 output rows, so every
     * even row equals the odd row below it. */
    bool doubled = true;
    for (int y = 0; y < app::kScreenHeight && doubled; y += 2) {
        for (int x = 0; x < app::kScreenWidth; ++x) {
            if (px[y * app::kScreenWidth + x] !=
                px[(y + 1) * app::kScreenWidth + x]) { doubled = false; break; }
        }
    }
    CHECK(doubled);
}

TEST_CASE("saveScreenshot writes a PNG at the requested path")
{
    ms0515::Emulator emu;
    app::Screen screen;
    screen.render(emu, 0);

    const fs::path out = fs::path(TESTS_BUILD_DIR) / "shot.png";
    std::error_code ec;
    fs::remove(out, ec);

    const std::string written = app::saveScreenshot(screen, out.string());
    CHECK(written == out.string());
    REQUIRE(fs::exists(out));

    const std::vector<unsigned char> bytes = readAll(out);
    REQUIRE(bytes.size() > 8);
    /* PNG signature. */
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) CHECK(bytes[size_t(i)] == sig[i]);

    fs::remove(out, ec);
}

TEST_CASE("saveScreenshot reports failure instead of throwing")
{
    ms0515::Emulator emu;
    app::Screen screen;
    screen.render(emu, 0);

    /* A path whose parent directory does not exist cannot be written. */
    const fs::path bad = fs::path(TESTS_BUILD_DIR) / "no-such-dir" / "x.png";
    CHECK(app::saveScreenshot(screen, bad.string()).empty());
}
