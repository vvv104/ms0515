/*
 * Video.cpp — frame composer.
 *
 * The lib walks VRAM and emits decoded pixel attributes via the
 * Emulator::forEach{Hi,Lo}ResPixel visitors; this file only handles
 * what's specific to display output: palette translation (3-bit GRB
 * → packed RGBA8888) and 2× vertical doubling into the framebuffer.
 *
 * Reference: docs/hardware/video.md
 */

#include "Video.hpp"
#include <ms0515/Emulator.hpp>

#include <cstring>

namespace ms0515_frontend {

namespace {

/* Build a packed RGBA8888 value (little-endian: 0xAABBGGRR). */
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

/* Write a source pixel into the 640x400 output at (x, y*2) and
 * (x, y*2+1) — vertical doubling is shared by both modes. */
inline void put2(uint32_t *frame, int x, int y, uint32_t color)
{
    frame[(y * 2 + 0) * kScreenWidth + x] = color;
    frame[(y * 2 + 1) * kScreenWidth + x] = color;
}

} /* anonymous namespace */

Video::Video() : frame_(kScreenWidth * kScreenHeight, 0) {}

uint32_t Video::paletteColor(int grb, bool bright)
{
    /* GRB layout: bit 2 = G, bit 1 = R, bit 0 = B. */
    bool g = (grb >> 2) & 1;
    bool r = (grb >> 1) & 1;
    bool b = (grb >> 0) & 1;
    uint8_t hi = bright ? 0xFF : 0x80;
    return rgba(r ? hi : 0, g ? hi : 0, b ? hi : 0);
}

void Video::render(const ms0515::Emulator &emu, uint32_t frameCounter)
{
    if (emu.isHires()) {
        /* Hi-res is two-colour: background = border, foreground =
         * complement of border (per the NS4 technical description).
         * Border comes from System Register C bits 2-0 (GRB). */
        const uint8_t border = emu.borderColor();
        const uint32_t bg = paletteColor(border        & 0x07, /*bright=*/true);
        const uint32_t fg = paletteColor((~border)     & 0x07, /*bright=*/true);
        emu.forEachHiResPixel([&](int x, int y, bool lit) {
            put2(frame_.data(), x, y, lit ? fg : bg);
        });
    } else {
        /* Flash phase toggles every ~30 frames (≈1.66 Hz at 50 Hz). */
        const bool flashOn = (frameCounter / 30) & 1;
        emu.forEachLoResPixel([&](int x, int y, bool lit,
                                   const ms0515::LoResAttr &a) {
            uint32_t fg = paletteColor(a.fgGrb, a.bright);
            uint32_t bg = paletteColor(a.bgGrb, a.bright);
            /* Flash: during the "off" half-period swap fg/bg so the
             * highlighted cell appears inverted. */
            if (a.flash && flashOn) {
                uint32_t t = fg; fg = bg; bg = t;
            }
            const uint32_t c = lit ? fg : bg;
            /* 320 → 640: each logical pixel becomes two horizontal pixels. */
            put2(frame_.data(), x * 2 + 0, y, c);
            put2(frame_.data(), x * 2 + 1, y, c);
        });
    }
}

} /* namespace ms0515_frontend */
