/*
 * Screen.hpp — VRAM-to-RGBA frame composer and PNG screenshot writer.
 *
 * Decodes the emulator's VRAM into a 640x400 RGBA framebuffer using the
 * current video mode:
 *   - 320x200 colour attribute mode: each logical pixel becomes a 2x2
 *     block, so the output resolution matches hires and both modes
 *     display at the same aspect ratio.
 *   - 640x200 monochrome mode: each logical pixel becomes a 1x2 block.
 * See docs/hardware/video.md for the bit layout.
 *
 * Lives in libapp rather than in the frontend because both binaries need
 * a picture: ms0515.exe uploads it to an SDL texture, ms0515-cli.exe
 * writes it out for --screenshot (which is how guest programs get probed
 * without a display).
 */

#ifndef MS0515_APP_SCREEN_HPP
#define MS0515_APP_SCREEN_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace ms0515 { class Emulator; }

namespace ms0515::app {

constexpr int kScreenWidth  = 640;
constexpr int kScreenHeight = 400;

class Screen {
public:
    Screen();

    /* Decode the emulator's current VRAM into the internal RGBA buffer.
     * `frameCounter` increments once per host frame — used to drive the
     * ~2 Hz flash attribute. */
    void render(const Emulator &emu, uint32_t frameCounter);

    /* Pointer to the RGBA8888 framebuffer (640x400x4 bytes). */
    [[nodiscard]] const uint32_t *pixels() const { return frame_.data(); }

    static constexpr int width()  { return kScreenWidth; }
    static constexpr int height() { return kScreenHeight; }

private:
    /* GRB → RGBA8888 lookup, with intensity (dim/bright). */
    static uint32_t paletteColor(int grb, bool bright);

    /* Heap-allocated to avoid a ~1 MB stack allocation on Windows. */
    std::vector<uint32_t> frame_;
};

/* Write `screen` to `path` as a PNG.  An empty path means a timestamped
 * name next to the executable (Paths::timestamped).  Returns the path
 * actually written, or an empty string if encoding or writing failed. */
std::string saveScreenshot(const Screen &screen, const std::string &path);

} /* namespace ms0515::app */

#endif /* MS0515_APP_SCREEN_HPP */
