/*
 * ms0515_web.cpp — the browser front-end's C API over the lib layer.
 *
 * Compiled with Emscripten into ms0515.wasm + ms0515.js.  The page (or a
 * Node smoke test) drives one Emulator through a handful of flat C entry
 * points: files live in the module's in-memory file system (the page
 * writes ROM / disk images there, reads written images back out), a frame
 * is one call, the picture and the sound of that frame are fetched with
 * two more.  Everything that is "web" — canvas, audio, keyboard events,
 * persistence — stays in JavaScript; nothing here knows about a browser.
 *
 * The picture is rendered the way the SDL front-end renders it
 * (Video.cpp): 640x400 RGBA, both modes line-doubled, lo-res pixels
 * doubled horizontally, the 3-bit GRB palette, flash swapping fg/bg every
 * 30 frames.  The sound is the front-end's too (Audio.cpp): the speaker
 * level transitions of the frame, stamped with their CPU cycle, resampled
 * to PCM at the rate the page asks for.
 */
#include <emscripten.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <ms0515/Emulator.hpp>

namespace {

constexpr int kWidth        = 640;
constexpr int kHeight       = 400;
constexpr int kCpuClockHz   = 7500000;
constexpr int16_t kAmplitude = 6000;

struct Transition {
    int cycle;
    int level;
};

struct Handle {
    ms0515::Emulator        emu;
    std::vector<uint32_t>   frame = std::vector<uint32_t>(kWidth * kHeight, 0);
    std::vector<Transition> transitions;
    int                     level      = 0;  /* the speaker level now */
    int                     startLevel = 0;  /* ... at the frame's start */
    int                     frameCycles = 0; /* CPU cycles the last frame ran */
    uint32_t                frameCount = 0;
};

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

uint32_t paletteColor(int grb, bool bright)
{
    const bool g = (grb >> 2) & 1;
    const bool r = (grb >> 1) & 1;
    const bool b = (grb >> 0) & 1;
    const uint8_t hi = bright ? 0xFF : 0x80;
    return rgba(r ? hi : 0, g ? hi : 0, b ? hi : 0);
}

inline void put2(uint32_t *frame, int x, int y, uint32_t color)
{
    frame[(y * 2 + 0) * kWidth + x] = color;
    frame[(y * 2 + 1) * kWidth + x] = color;
}

void render(Handle &h)
{
    uint32_t *frame = h.frame.data();
    const ms0515::Emulator &emu = h.emu;
    if (emu.isHires()) {
        const uint8_t border = emu.borderColor();
        const uint32_t bg = paletteColor(border    & 0x07, true);
        const uint32_t fg = paletteColor((~border) & 0x07, true);
        emu.forEachHiResPixel([&](int x, int y, bool lit) {
            put2(frame, x, y, lit ? fg : bg);
        });
    } else {
        const bool flashOn = (h.frameCount / 30) & 1;
        emu.forEachLoResPixel([&](int x, int y, bool lit, const ms0515::LoResAttr &a) {
            uint32_t fg = paletteColor(a.fgGrb, a.bright);
            uint32_t bg = paletteColor(a.bgGrb, a.bright);
            if (a.flash && flashOn)
                std::swap(fg, bg);
            const uint32_t c = lit ? fg : bg;
            put2(frame, x * 2 + 0, y, c);
            put2(frame, x * 2 + 1, y, c);
        });
    }
}

} /* namespace */

extern "C" {

EMSCRIPTEN_KEEPALIVE Handle *ms_create(void)
{
    auto *h = new Handle;
    h->emu.setSoundCallback([h](int value) {
        h->transitions.push_back({static_cast<int>(h->emu.frameCyclePos()), value});
        h->level = value;
    });
    return h;
}

EMSCRIPTEN_KEEPALIVE void ms_destroy(Handle *h) { delete h; }

EMSCRIPTEN_KEEPALIVE void ms_reset(Handle *h) { h->emu.reset(); }

/* The ROM file at `path` in the module's file system. */
EMSCRIPTEN_KEEPALIVE int ms_load_rom(Handle *h, const char *path)
{
    return h->emu.loadRomFile(path) ? 1 : 0;
}

/* Mount the image at `path` on FDC unit 0..3 (= side * 2 + drive: FD0 and
 * FD1 are the drives' side 0, FD2 and FD3 their side 1, as the OS numbers
 * DZ0..DZ3).  A double-sided image is mounted on both units of its drive,
 * as the host front-ends do. */
EMSCRIPTEN_KEEPALIVE int ms_mount(Handle *h, int unit, const char *path)
{
    return h->emu.mountDisk(unit, path) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void ms_unmount(Handle *h, int unit) { h->emu.unmountDisk(unit); }

EMSCRIPTEN_KEEPALIVE int ms_disk_active(Handle *h, int unit)
{
    return h->emu.diskActive(unit) ? 1 : 0;
}

/* The paravirtual hard disk (HD:): an image of any size that is a multiple
 * of 512 bytes; mounting presents the controller on the bus. */
EMSCRIPTEN_KEEPALIVE int ms_mount_hd(Handle *h, const char *path)
{
    return h->emu.mountHd(path) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void ms_unmount_hd(Handle *h) { h->emu.unmountHd(); }

EMSCRIPTEN_KEEPALIVE int ms_hd_active(Handle *h) { return h->emu.hdActive() ? 1 : 0; }

/* Run one 50 Hz frame.  Returns the CPU cycles it took (0 = halted). */
EMSCRIPTEN_KEEPALIVE int ms_frame(Handle *h)
{
    h->transitions.clear();
    h->startLevel = h->level;
    const bool running = h->emu.stepFrame();
    h->frameCycles = static_cast<int>(h->emu.frameCyclePos());
    ++h->frameCount;
    return running ? h->frameCycles : 0;
}

/* The picture of the machine now: 640 x 400 RGBA (little-endian ABGR
 * words, i.e. bytes R G B A as a canvas ImageData wants). */
EMSCRIPTEN_KEEPALIVE const uint32_t *ms_render(Handle *h)
{
    render(*h);
    return h->frame.data();
}

EMSCRIPTEN_KEEPALIVE int ms_width(void)  { return kWidth; }
EMSCRIPTEN_KEEPALIVE int ms_height(void) { return kHeight; }

/* The last frame's sound as signed 16-bit PCM at `rate` Hz into `out`
 * (up to `max` samples); returns the samples written. */
EMSCRIPTEN_KEEPALIVE int ms_audio(Handle *h, int16_t *out, int max, int rate)
{
    const int cycles = h->frameCycles;
    if (cycles <= 0 || max <= 0)
        return 0;
    int n = static_cast<int>(static_cast<int64_t>(rate) * cycles / kCpuClockHz);
    if (n <= 0) n = 1;
    if (n > max) n = max;
    size_t t = 0;
    int level = h->startLevel;
    for (int i = 0; i < n; ++i) {
        const int cycle = static_cast<int>(static_cast<int64_t>(i) * cycles / n);
        while (t < h->transitions.size() && h->transitions[t].cycle <= cycle)
            level = h->transitions[t++].level;
        out[i] = level ? kAmplitude : -kAmplitude;
    }
    return n;
}

/* Diagnostics for the page's __ms(): the speaker transitions of the last
 * frame, and system register C (0177604) as the guest left it. */
EMSCRIPTEN_KEEPALIVE int ms_transitions(Handle *h)
{
    return static_cast<int>(h->transitions.size());
}

EMSCRIPTEN_KEEPALIVE int ms_reg_c(Handle *h) { return h->emu.readByte(0177604); }

/* Keys: `key` is ms0515::Key's value (the page carries the same table),
 * `down` 1 / 0.  ms_key_tick drives the MS7004 auto-repeat clock. */
EMSCRIPTEN_KEEPALIVE void ms_key(Handle *h, int key, int down)
{
    h->emu.keyPress(static_cast<ms0515::Key>(key), down != 0);
}

EMSCRIPTEN_KEEPALIVE void ms_key_release_all(Handle *h) { h->emu.keyReleaseAll(); }

/* The joystick on the MS7007 port: bits 0-4 = right, left, down, up, fire. */
EMSCRIPTEN_KEEPALIVE void ms_joystick(Handle *h, int bits)
{
    h->emu.setJoystick(static_cast<uint8_t>(bits));
}

EMSCRIPTEN_KEEPALIVE void ms_key_tick(Handle *h, uint32_t now_ms) { h->emu.keyTick(now_ms); }

/* The highest ms0515::Key value - the page checks its table against it. */
EMSCRIPTEN_KEEPALIVE int ms_key_max(void) { return static_cast<int>(ms0515::Key::KpMinus); }

/* The keyboard's lamps and held keys, for the page's host-key mapping
 * (РУС/ЛАТ picks the letter map, CAPS + Shift inverts the case). */
EMSCRIPTEN_KEEPALIVE int ms_ruslat(Handle *h) { return h->emu.ruslatOn() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int ms_caps(Handle *h) { return h->emu.capsOn() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int ms_key_held(Handle *h, int key)
{
    return h->emu.keyHeld(static_cast<ms0515::Key>(key)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int ms_save_state(Handle *h, const char *path)
{
    return h->emu.saveState(path).has_value() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int ms_load_state(Handle *h, const char *path)
{
    return h->emu.loadState(path).has_value() ? 1 : 0;
}

} /* extern "C" */
