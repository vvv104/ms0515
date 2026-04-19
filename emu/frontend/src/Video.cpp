/*
 * Video.cpp — VRAM decoder.
 *
 * Reference: docs/video.md
 *
 * 320x200 mode: each 16-bit word = 8 pixels.  Low byte holds the pixel
 * data (D7 = leftmost).  High byte holds the attribute byte:
 *   bit 7  F  flash    (swap fg/bg at ~2 Hz)
 *   bit 6  I  intensity (1 = bright)
 *   bits 5-3  G',R',B'  background color
 *   bits 2-0  G,R,B     foreground color
 * A pixel data bit of 1 selects foreground, 0 selects background.
 *
 * 640x200 mode: each word is 16 mono pixels, shifted out low byte
 * first.  Foreground is the complement of the border; background is
 * the border.
 *
 * Output is always a 640x400 RGBA8888 image so both modes use the same
 * texture and the frontend can display them at a fixed aspect ratio.
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
    const uint8_t *vram = emu.vram();
    if (emu.isHires()) {
        renderHiRes(vram, emu.borderColor());
    } else {
        /* Flash phase toggles every ~30 frames (≈1.66 Hz at 50 Hz). */
        bool flashOn = (frameCounter / 30) & 1;
        renderLowRes(vram, flashOn);
    }
}

void Video::renderLowRes(const uint8_t *vram, bool flashOn)
{
    /* 320x200 → 640x400: each logical pixel becomes a 2x2 block. */
    for (int y = 0; y < 200; ++y) {
        const uint8_t *row = vram + y * 80;   /* 40 words per line */
        for (int wx = 0; wx < 40; ++wx) {
            uint8_t pixData = row[wx * 2 + 0];
            uint8_t attr    = row[wx * 2 + 1];
            bool    flash   = (attr >> 7) & 1;
            bool    bright  = (attr >> 6) & 1;
            int     bgGrb   = (attr >> 3) & 0x07;
            int     fgGrb   = (attr >> 0) & 0x07;
            uint32_t fg     = paletteColor(fgGrb, bright);
            uint32_t bg     = paletteColor(bgGrb, bright);
            /* Flash: during the "off" half-period swap fg/bg so the
             * highlighted cell appears inverted. */
            if (flash && flashOn) {
                uint32_t t = fg; fg = bg; bg = t;
            }
            for (int p = 0; p < 8; ++p) {
                bool     lit = (pixData >> (7 - p)) & 1;
                uint32_t c   = lit ? fg : bg;
                int      x   = wx * 16 + p * 2;
                put2(frame_.data(), x + 0, y, c);
                put2(frame_.data(), x + 1, y, c);
            }
        }
    }
}

void Video::renderHiRes(const uint8_t *vram, uint8_t border)
{
    /* 640x200 is two-color, not monochrome.  Per the NS4 technical
     * description: background = border color, foreground = complement
     * of the border color.  Border comes from System Register C bits
     * 2-0 (GRB).  The OS typically sets border = 7 (white) on boot,
     * giving a white background with black text.  BLACK.SAV / BLUE.SAV
     * on distribution disks change the border color to recolor the
     * entire screen. */
    int      bgGrb = border & 0x07;
    int      fgGrb = (~border) & 0x07;
    uint32_t bg    = paletteColor(bgGrb, /*bright=*/true);
    uint32_t fg    = paletteColor(fgGrb, /*bright=*/true);

    /* 640x200 → 640x400: 1x horizontal, 2x vertical. */
    for (int y = 0; y < 200; ++y) {
        const uint8_t *row = vram + y * 80;
        for (int wx = 0; wx < 40; ++wx) {
            uint8_t lo = row[wx * 2 + 0];   /* shifted out first */
            uint8_t hi = row[wx * 2 + 1];   /* shifted out second */
            for (int p = 0; p < 8; ++p) {
                bool lit = (lo >> (7 - p)) & 1;
                put2(frame_.data(), wx * 16 + p, y, lit ? fg : bg);
            }
            for (int p = 0; p < 8; ++p) {
                bool lit = (hi >> (7 - p)) & 1;
                put2(frame_.data(), wx * 16 + 8 + p, y, lit ? fg : bg);
            }
        }
    }
}

} /* namespace ms0515_frontend */
