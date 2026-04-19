/*
 * Video.hpp — VRAM-to-RGBA decoder for the MS0515 video controller.
 *
 * Decodes the 16 KB VRAM into a 640x400 RGBA framebuffer using the
 * current video mode:
 *   - 320x200 colour attribute mode: each logical pixel becomes a 2x2
 *     block, so the output resolution matches hires and both modes
 *     display at the same aspect ratio.
 *   - 640x200 monochrome mode: each logical pixel becomes a 1x2 block.
 * See docs/video.md for the bit layout.
 */

#ifndef MS0515_FRONTEND_VIDEO_HPP
#define MS0515_FRONTEND_VIDEO_HPP

#include <cstdint>
#include <vector>

namespace ms0515 {
class Emulator;
}

namespace ms0515_frontend {

constexpr int kScreenWidth  = 640;
constexpr int kScreenHeight = 400;

class Video {
public:
    Video();

    /* Decode the emulator's current VRAM into the internal RGBA buffer.
     * `frameCounter` increments once per host frame — used to drive the
     * ~2 Hz flash attribute. */
    void render(const ms0515::Emulator &emu, uint32_t frameCounter);

    /* Pointer to the RGBA8888 framebuffer (640x400x4 bytes). */
    const uint32_t *pixels() const { return frame_.data(); }

    static constexpr int width()  { return kScreenWidth; }
    static constexpr int height() { return kScreenHeight; }

private:
    void renderLowRes(const uint8_t *vram, bool flashOn);
    void renderHiRes (const uint8_t *vram, uint8_t border);

    /* GRB → RGBA8888 lookup, with intensity (dim/bright). */
    static uint32_t paletteColor(int grb, bool bright);

    /* Heap-allocated to avoid a ~1 MB stack allocation on Windows. */
    std::vector<uint32_t> frame_;
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_VIDEO_HPP */
