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
 * (libapp Screen.cpp): 640x400 RGBA, both modes line-doubled, lo-res pixels
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
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <fstream>
#include <iterator>
#include <string>

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

/* The RT-11 file system of an image file (src/disk): the page's file
 * panel.  Results and errors live in statics the page reads right after
 * the call. */
std::string          gDiskText;   /* the directory as JSON */
std::string          gDiskError;  /* the last failure's reason */
std::vector<uint8_t> gDiskBytes;  /* the last file read */

std::vector<uint8_t> readAll(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool writeAll(const char *path, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::string jsonEscape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/* The JS side names a volume kind by the disk lib's Vol value: 0 a
 * floppy side, 1 a linear HD/LD container, 2 DV:, 3 MZ:. */
ms0515::disk::Vol volOf(int vol) { return static_cast<ms0515::disk::Vol>(vol); }

bool dsFor(int vol, const std::vector<uint8_t> &bytes)
{
    return volOf(vol) == ms0515::disk::Vol::floppy && bytes.size() == 2 * 409600;
}

} /* namespace */

extern "C" {

/* The RT-11 file system of an image in the module's file system - the
 * page's commander.  `vol` picks the addressing (the disk lib's Vol):
 * 0 a floppy side (`side` picks it), 1 an HD / LD container (byte =
 * LBN * 512), 2 a DV: whole-disk volume, 3 an MZ: one.  The directory
 * as JSON, in the
 * directory's order: {"free": the free blocks, "files": [{name, blocks,
 * date ("YYYY-MM-DD" or ""), protected} for a file, {empty: 1, blocks} for
 * an unused area]}; "" with ms_disk_error() when there is no directory (a
 * blank volume: ms_disk_init makes one). */
EMSCRIPTEN_KEEPALIVE const char *ms_disk_dir(const char *path, int side, int vol)
{
    gDiskText.clear();
    auto bytes = readAll(path);
    auto img = ms0515::disk::openVolume(bytes, volOf(vol), side);
    if (!img || !img->hasDirectory) { gDiskError = "no RT-11 directory here"; return ""; }
    long free = 0;
    for (const auto &e : img->directory.entries)
        if (e.isEmpty()) free += e.length;
    /* The home block's volume id and owner: 12 bytes each as they are (the
     * page decodes them - the OS writes them in its terminal's encoding). */
    auto field = [&](int off) {
        auto home = img->block(1);
        std::string s = "[";
        for (int i = 0; i < 12 && home.size() == 512; ++i) s += (i ? "," : "") + std::to_string(home[static_cast<size_t>(off + i)]);
        return s + "]";
    };
    std::string out = "{\"vol\":" + std::to_string(vol) + ",\"free\":" + std::to_string(free) + ",\"volumeId\":" + field(0x1D8)
                    + ",\"owner\":" + field(0x1E4) + ",\"segments\":" + std::to_string(img->directory.segsTotal)
                    + ",\"files\":[";
    int ordinal = -1;
    for (const auto &e : img->directory.entries) {
        ++ordinal;
        std::string date;
        if (e.date) {
            const auto d = ms0515::disk::decodeDate(e.date);
            char buf[16];
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.year, d.month, d.day);
            date = buf;
        }
        if (e.isEmpty() && e.length > 0) {
            /* An unused area; the file it was, when the OS's DELETE left
             * the name (a put over the area leaves the sentinel instead). */
            const bool was = !e.name.empty() && e.name[0] != ' ' && e.name != "EMPTY.FIL";
            if (out.back() != '[') out += ",";
            out += "{\"empty\":1,\"blocks\":" + std::to_string(e.length) + ",\"i\":" + std::to_string(ordinal)
                 + (was ? ",\"was\":\"" + jsonEscape(e.name) + "\",\"date\":\"" + date + "\"" : "") + "}";
            continue;
        }
        if (!e.isPermanent()) continue;
        if (out.back() != '[') out += ",";
        out += "{\"name\":\"" + jsonEscape(e.name) + "\",\"blocks\":" + std::to_string(e.length)
             + ",\"date\":\"" + date + "\",\"protected\":" + ((e.status & ms0515::disk::kStatusProtected) ? "1" : "0") + "}";
    }
    gDiskText = out + "]}";
    return gDiskText.c_str();
}

/* Content-based format detection: every volume kind whose home block
 * points at a directory that parses, as JSON [{"vol":N,"side":S},...] -
 * vol as above.  An 800 KB image may be two DZ sides, a DV: or an MZ:
 * whole-disk volume; a blank detects as nothing. */
EMSCRIPTEN_KEEPALIVE const char *ms_disk_detect(const char *path)
{
    static std::string out;
    auto bytes = readAll(path);
    const auto specs = ms0515::disk::detectVolumes(bytes);
    out = "[";
    for (const auto &s : specs) {
        if (out.size() > 1) out += ",";
        out += "{\"vol\":" + std::to_string(static_cast<int>(s.vol))
             + ",\"side\":" + std::to_string(s.side) + "}";
    }
    out += "]";
    return out.c_str();
}

/* Squeeze: the files packed to the front, one free area at the end.  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_squeeze(const char *path, int side, int vol)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::squeeze(bytes, side, dsFor(vol, bytes), volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* An RT-11 directory on a blank volume (the disk library's INIT): a floppy
 * side, or the whole linear image; the volume id and the owner as bytes
 * (12 at most each - the page encodes them as the OS's terminal would,
 * KOI-8R for Cyrillic), `segments` directory segments (1..31).  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_init(const char *path, int side, int vol, const char *volumeId, int idLen,
                                      const char *owner, int ownerLen, int segments)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::InitOptions opts;
        opts.volumeId.assign(volumeId, static_cast<size_t>(idLen));
        opts.owner.assign(owner, static_cast<size_t>(ownerLen));
        opts.segments = segments;
        ms0515::disk::initVolume(bytes, side, dsFor(vol, bytes), opts, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const char *ms_disk_error(void) { return gDiskError.c_str(); }

/* INITIALIZE/VOLUMEID:ONLY - the volume id and the owner written, the
 * directory and the files kept.  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_volume_id(const char *path, int side, int vol, const char *volumeId, int idLen,
                                           const char *owner, int ownerLen)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::setVolumeId(bytes, side, dsFor(vol, bytes),
                                  std::string(volumeId, static_cast<size_t>(idLen)), std::string(owner, static_cast<size_t>(ownerLen)), volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* Read a file: the size, its bytes at ms_disk_data(); -1 when absent. */
EMSCRIPTEN_KEEPALIVE int ms_disk_get(const char *path, int side, int vol, const char *name)
{
    gDiskBytes.clear();
    auto bytes = readAll(path);
    auto img = ms0515::disk::openVolume(bytes, volOf(vol), side);
    if (!img || !img->hasDirectory || !img->directory.find(name)) { gDiskError = "no such file"; return -1; }
    gDiskBytes = img->readFile(name);
    return static_cast<int>(gDiskBytes.size());
}

EMSCRIPTEN_KEEPALIVE const uint8_t *ms_disk_data(void) { return gDiskBytes.data(); }

/* A blank floppy as the machine's formatting leaves it - the 0xB6 0x6D
 * pattern on every byte, no RT-11 structure (a block still so was never
 * written: what tells data from nothing on a badly read disk).  ds for
 * two-sided.  The size, the bytes at ms_disk_data(). */
EMSCRIPTEN_KEEPALIVE int ms_disk_blank(int ds)
{
    gDiskBytes = ms0515::disk::blankImage(ds != 0);
    return static_cast<int>(gDiskBytes.size());
}

/* The blocks of the directory entry at `ordinal` (the "i" of ms_disk_dir) -
 * an unused area's, say - the size, the bytes at ms_disk_data(); -1 when
 * there is no such entry. */
EMSCRIPTEN_KEEPALIVE int ms_disk_area(const char *path, int side, int vol, int ordinal)
{
    gDiskBytes.clear();
    auto bytes = readAll(path);
    auto img = ms0515::disk::openVolume(bytes, volOf(vol), side);
    if (!img || !img->hasDirectory || ordinal < 0 || static_cast<size_t>(ordinal) >= img->directory.entries.size()) {
        gDiskError = "no such directory entry";
        return -1;
    }
    const auto &e = img->directory.entries[static_cast<size_t>(ordinal)];
    for (int i = 0; i < e.length; ++i) {
        auto b = img->block(e.startBlock + i);
        if (b.size() == 512) gDiskBytes.insert(gDiskBytes.end(), b.begin(), b.end());
        else                 gDiskBytes.insert(gDiskBytes.end(), 512, 0);
    }
    return static_cast<int>(gDiskBytes.size());
}

/* Write a file (replacing one of the name); year 0 = no date.  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_put(const char *path, int side, int vol, const char *name,
                                     const uint8_t *data, int len, int year, int month, int day, int prot)
{
    try {
        auto bytes = readAll(path);
        const bool ds = dsFor(vol, bytes);
        auto img = ms0515::disk::openVolume(bytes, volOf(vol), side);
        if (img && img->hasDirectory && img->directory.find(name))
            ms0515::disk::removeFile(bytes, side, ds, name, volOf(vol));
        ms0515::disk::PutOptions opts;
        if (year) opts.date = ms0515::disk::encodeDate(year, month, day);
        opts.readOnly = prot != 0;
        ms0515::disk::putFile(bytes, side, ds, name, std::span<const uint8_t>(data, static_cast<size_t>(len)), opts, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE int ms_disk_rm(const char *path, int side, int vol, const char *name)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::removeFile(bytes, side, dsFor(vol, bytes), name, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE int ms_disk_rename(const char *path, int side, int vol, const char *name, const char *newName)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::renameFile(bytes, side, dsFor(vol, bytes), name, newName, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* An unused area's deleted file brought back: `ordinal` is the entry's
 * place in the directory, the "i" of ms_disk_dir; `newName` the name to
 * come back under ("" - the one it had).  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_undelete(const char *path, int side, int vol, int ordinal, const char *newName)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::undeleteEntry(bytes, side, dsFor(vol, bytes), ordinal, newName, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* The monitor a floppy side boots ("RT11SJ", "MON8SJ", ...), "" when the
 * side has no bootstrap. */
EMSCRIPTEN_KEEPALIVE const char *ms_disk_booted(const char *path, int side, int vol)
{
    auto bytes = readAll(path);
    gDiskText = ms0515::disk::bootedMonitor(bytes, side, bytes.size() == 2 * 409600, volOf(vol));
    return gDiskText.c_str();
}

/* The kit a system volume of the side would be made of, comma-separated:
 * the monitor, SWAP, DZ, TT, PIP, DUP, DIR, RESORC of those there; "" when
 * the side does not boot. */
EMSCRIPTEN_KEEPALIVE const char *ms_disk_kit(const char *path, int side, int vol, int target)
{
    auto bytes = readAll(path);
    const bool ds = bytes.size() == 2 * 409600;
    gDiskText.clear();
    const std::string monitor = ms0515::disk::bootedMonitor(bytes, side, ds, volOf(vol));
    if (!monitor.empty())
        for (const auto &name : ms0515::disk::systemKit(bytes, side, ds, monitor, volOf(vol), volOf(target))) gDiskText += (gDiskText.empty() ? "" : ",") + name;
    return gDiskText.c_str();
}

/* The target side made a system volume of the source side's (the disk
 * library's makeSystemVolume: the kit protected, the extras - comma-
 * separated names - as they are, the startup .COM made anew, the
 * bootstrap).  The monitor's name at ms_disk_text(); 1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_system(const char *path, int side, int vol, const char *fromPath, int fromSide, int fromVol, const char *extras)
{
    try {
        auto bytes = readAll(path);
        auto from = readAll(fromPath);
        std::vector<std::string> names;
        for (std::string s = extras; !s.empty();) {
            const auto comma = s.find(',');
            names.push_back(s.substr(0, comma));
            s = comma == std::string::npos ? "" : s.substr(comma + 1);
        }
        gDiskText = ms0515::disk::makeSystemVolume(bytes, side, bytes.size() == 2 * 409600, from, fromSide, from.size() == 2 * 409600, names, volOf(vol), volOf(fromVol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* The emulator's version, as built (src/VERSION). */
EMSCRIPTEN_KEEPALIVE const char *ms_version(void) { return MS0515_VERSION_STRING; }

EMSCRIPTEN_KEEPALIVE const char *ms_disk_text(void) { return gDiskText.c_str(); }

/* /PROTECT (on = 1) or /NOPROTECT (0) on a file.  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_protect(const char *path, int side, int vol, const char *name, int on)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::setProtected(bytes, side, dsFor(vol, bytes), name, on != 0, volOf(vol));
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* A linear image (a logical disk) enlarged by `blocks` - a file, it can
 * grow - so that a file the commander puts in fits.  1 / 0. */
EMSCRIPTEN_KEEPALIVE int ms_disk_grow(const char *path, int blocks)
{
    try {
        auto bytes = readAll(path);
        ms0515::disk::growLinear(bytes, blocks);
        if (!writeAll(path, bytes)) { gDiskError = "cannot write the image"; return 0; }
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

/* A logical disk built in memory - the file the OS's LD handler mounts as a
 * volume (linear, no interleave, any size).  ms_ld_create sizes it in
 * blocks and initialises it (`segments` directory segments, the volume id);
 * ms_ld_put adds a file; ms_ld_data / ms_ld_size hand the bytes over. */
static std::vector<uint8_t> gLd;

EMSCRIPTEN_KEEPALIVE int ms_ld_create(int blocks, int segments, const char *volumeId)
{
    try {
        gLd = ms0515::disk::blankLinear(blocks);
        ms0515::disk::InitOptions opts;
        opts.volumeId = volumeId;
        opts.segments = segments;
        ms0515::disk::initVolume(gLd, 0, false, opts, ms0515::disk::Vol::linear);
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE int ms_ld_put(const char *name, const uint8_t *data, int len, int year, int month, int day, int prot)
{
    try {
        ms0515::disk::PutOptions opts;
        if (year) opts.date = ms0515::disk::encodeDate(year, month, day);
        opts.readOnly = prot != 0;
        ms0515::disk::putFile(gLd, 0, false, name, std::span<const uint8_t>(data, static_cast<size_t>(len)), opts, ms0515::disk::Vol::linear);
        return 1;
    } catch (const std::exception &e) {
        gDiskError = e.what();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const uint8_t *ms_ld_data(void) { return gLd.data(); }
EMSCRIPTEN_KEEPALIVE int ms_ld_size(void) { return static_cast<int>(gLd.size()); }

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
