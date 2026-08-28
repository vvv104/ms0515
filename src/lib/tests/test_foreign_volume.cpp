/*
 * test_foreign_volume.cpp — DV:/MZ: whole-disk volumes against the real OS.
 *
 * The oracle direction the unit tests cannot give: a volume the disk
 * library builds with Vol::dv / Vol::mz is handed to the OS running in
 * the emulator (vvv's RT-11 with the omega kit's DV.SYS / MZ.SYS put on
 * its system disk), and the OS must
 *   1. list the file we put there (`DIR DV1:` / `DIR MZ1:`), and
 *   2. copy it onto the system disk (`COPY DV1:X SY:Y`), whose bytes the
 *      library then reads back — identical, so the OS truly read the
 *      data through its own handler where the library wrote it.
 *
 * The layout itself (track = LBN/10, +2 for DV, wrap at 160, natural
 * sides, no interleave) came from the handlers' disassembled translate
 * code; this keeps the emulator FDC (shared head position per drive!)
 * and the library agreeing with the handlers for good.
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Layout.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif

using namespace ms0515::disk;

namespace {

const std::string kDisks = std::string{ASSETS_DIR} + "/disks";
const std::string kRomA  = std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom";

std::vector<uint8_t> readAll(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

void writeAll(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void stepFrames(ms0515::Emulator &emu, int n)
{
    for (int i = 0; i < n; ++i) (void)emu.stepFrame();
}

void tap(ms0515::Emulator &emu, ms0515::Key key)
{
    emu.keyPress(key, true);
    stepFrames(emu, 2);
    emu.keyPress(key, false);
    stepFrames(emu, 8);
}

ms0515::Key letterKey(char c)
{
    using K = ms0515::Key;
    static constexpr K letters[26] = {
        K::A, K::B, K::C, K::D, K::E, K::F, K::G, K::H, K::I, K::J, K::K,
        K::L, K::M, K::N, K::O, K::P, K::Q, K::R, K::S, K::T, K::U, K::V,
        K::W, K::X, K::Y, K::Z,
    };
    return letters[c - 'A'];
}

void typeLine(ms0515::Emulator &emu, const char *s)
{
    using K = ms0515::Key;
    static constexpr K digits[10] = {
        K::Digit0, K::Digit1, K::Digit2, K::Digit3, K::Digit4,
        K::Digit5, K::Digit6, K::Digit7, K::Digit8, K::Digit9,
    };
    for (const char *p = s; *p; ++p) {
        const char c = *p;
        if (c >= 'A' && c <= 'Z')      tap(emu, letterKey(c));
        else if (c >= '0' && c <= '9') tap(emu, digits[c - '0']);
        else if (c == ' ')             tap(emu, K::Space);
        else if (c == ':')             tap(emu, K::ColonStar);
        else if (c == '.')             tap(emu, K::Period);
        else if (c == '/')             tap(emu, K::Slash);
    }
    tap(emu, K::Return);
}

/* Step until the floppies stay quiet for `quiet` frames (cap frames at
 * most) — commands like COPY do long FDC stretches with no output. */
void waitForDiskIdle(ms0515::Emulator &emu, int quiet, int cap)
{
    int q = 0;
    for (int i = 0; i < cap; ++i) {
        bool active = false;
        for (int u = 0; u < 4; ++u) if (emu.diskActive(u)) active = true;
        (void)emu.stepFrame();
        if (active) q = 0; else ++q;
        if (q >= quiet) return;
    }
}

std::vector<std::string> screenRows(const ms0515::Emulator &emu)
{
    ms0515::Terminal term;
    auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        std::string row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        rows.push_back(row);
    }
    return rows;
}

bool anyRowHas(const std::vector<std::string> &rows, const std::string &what)
{
    return std::any_of(rows.begin(), rows.end(), [&](const std::string &r) {
        return r.find(what) != std::string::npos;
    });
}

}  /* namespace */

TEST_CASE("the OS reads a DV/MZ volume the library made, through its own handler")
{
    namespace fs = std::filesystem;

    /* The system disk: vvv's RT-11 with the omega kit's DV/MZ handlers —
     * one sysgen family, the handlers install at boot. */
    const auto omega = openImage(readAll(kDisks + "/omega-lang.dsk"));
    REQUIRE(omega);
    auto system = readAll(kDisks + "/vvv.dsk");
    REQUIRE(system.size() == kSideSize);
    for (const char *handler : {"DV.SYS", "MZ.SYS"}) {
        const auto bytes = omega->readFile(handler);
        REQUIRE(!bytes.empty());
        putFile(system, 0, false, handler, bytes);
    }

    /* The payload: a recognisable 3-block text file. */
    std::vector<uint8_t> payload;
    for (int i = 0; i < 3 * kBlock / 16; ++i) {
        const std::string chunk = "MARCO POLO " + std::to_string(i % 10) + "\r\n  ";
        payload.insert(payload.end(), chunk.begin(), chunk.end());
    }

    for (const Vol vol : {Vol::dv, Vol::mz}) {
        const char *dev = (vol == Vol::dv) ? "DV1:" : "MZ1:";
        SUBCASE(dev) {
            auto volume = blankImage(true);
            initVolume(volume, 0, true, {}, vol);
            putFile(volume, 0, true, "HELLO.DAT", payload, {}, vol);

            std::error_code ec;
            fs::create_directories(TESTS_BUILD_DIR "/temp", ec);
            const fs::path sysPath = fs::path{TESTS_BUILD_DIR "/temp"} / "foreign_sys.dsk";
            const fs::path volPath = fs::path{TESTS_BUILD_DIR "/temp"} / "foreign_vol.dsk";
            writeAll(sysPath, system);
            writeAll(volPath, volume);
            {
                ms0515::Emulator emu;
                REQUIRE(emu.loadRomFile(kRomA));
                REQUIRE(emu.mountDisk(0, sysPath.string()));   /* drive 0 side 0 */
                REQUIRE(emu.mountDisk(1, volPath.string()));   /* drive 1, both sides */
                REQUIRE(emu.mountDisk(3, volPath.string()));
                emu.reset();
                stepFrames(emu, 700);                          /* boot to the prompt */
                REQUIRE(anyRowHas(screenRows(emu), "."));

                const std::string dir = std::string("DIR ") + dev;
                typeLine(emu, dir.c_str());
                waitForDiskIdle(emu, 50, 1500);
                CHECK_MESSAGE(anyRowHas(screenRows(emu), "HELLO"), dir);

                const std::string copy = std::string("COPY/NOQUERY ") + dev + "HELLO.DAT SY:ECHO.DAT";
                typeLine(emu, copy.c_str());
                waitForDiskIdle(emu, 100, 3000);
            }
            auto sys = loadImage(sysPath.string());
            REQUIRE(sys);
            CHECK(sys->readFile("ECHO.DAT") == payload);

            fs::remove(sysPath, ec);
            fs::remove(volPath, ec);
        }
    }
}

TEST_CASE("a DV system volume the library makes boots the machine")
{
    namespace fs = std::filesystem;

    /* The source: vvv's system floppy with the omega kit's DV.SYS put on
     * it - systemKit adds the target's own handler to the kit. */
    const auto omega = openImage(readAll(kDisks + "/omega-lang.dsk"));
    REQUIRE(omega);
    auto system = readAll(kDisks + "/vvv.dsk");
    putFile(system, 0, false, "DV.SYS", omega->readFile("DV.SYS"));

    auto target = blankImage(true);
    initVolume(target, 0, true, {}, Vol::dv);
    const std::string monitor =
        makeSystemVolume(target, 0, true, system, 0, false, {}, Vol::dv, Vol::floppy);
    CHECK(monitor == "RT11SJ");
    CHECK(bootedMonitor(target, 0, true, Vol::dv) == "RT11SJ");
    {
        auto im = openVolume(target, Vol::dv);
        REQUIRE(im);
        CHECK(im->directory.find("DV.SYS") != nullptr);   /* the boot's own driver */
        CHECK(im->directory.find("STARTS.COM") != nullptr);
    }

    /* And the ROM boots it: the whole-disk image in drive 0, both side
     * units, to the "." prompt - boot diskettes of this kind never
     * existed on the real machine. */
    std::error_code ec;
    fs::create_directories(TESTS_BUILD_DIR "/temp", ec);
    const fs::path path = fs::path{TESTS_BUILD_DIR "/temp"} / "dv_system.dsk";
    writeAll(path, target);
    {
        ms0515::Emulator emu;
        REQUIRE(emu.loadRomFile(kRomA));
        REQUIRE(emu.mountDisk(0, path.string()));
        REQUIRE(emu.mountDisk(2, path.string()));
        emu.reset();
        stepFrames(emu, 700);
        const auto rows = screenRows(emu);
        const bool prompt = std::any_of(rows.begin(), rows.end(),
            [](const std::string &r) { return !r.empty() && r[0] == '.'; });
        CHECK(prompt);
    }
    fs::remove(path, ec);
}
