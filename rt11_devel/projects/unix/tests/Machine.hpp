/*
 * Machine.hpp - the system's diskette booted on the native ROM-B, for the
 * tests.
 *
 * The image is the project's build (UNIX_DSK), or the file named with
 * --unix-dsk=<file>.  A double-sided image goes in as FD0 and FD2, its two
 * sides, as on the machine; a single-sided one, 409600 bytes, as FD0 alone.
 */
#pragma once

#include <doctest/doctest.h>

#include "EmulatorInternal.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/Typist.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace unixos {

namespace fs = std::filesystem;

/* The harness options: --unix-<name>=<value>. */
inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> o;
    return o;
}

inline fs::path imagePath()
{
    auto it = options().find("dsk");
    return it != options().end() ? fs::path(it->second) : fs::path(UNIX_DSK);
}

/* Whether the image is there to test; a missing one named by the option
 * fails, a missing default one skips. */
inline bool built()
{
    if (fs::exists(imagePath())) return true;
    if (options().count("dsk")) FAIL("no image at " << imagePath().string());
    return false;
}

inline std::vector<uint8_t> readImage()
{
    std::ifstream in(imagePath(), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The image without /etc/rc, beside the system's in the build folder: init
 * goes straight to getty, nothing mounted, no swap - for the tests that do
 * it by hand. */
inline std::vector<uint8_t> readBareImage()
{
    std::ifstream in(imagePath().parent_path() / "bare.dsk", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The single-sided system's image beside the double-sided one: one side,
 * 800 blocks, as the other emulators take a diskette. */
inline std::vector<uint8_t> readImage400()
{
    std::ifstream in(imagePath().parent_path() / "unix400.dsk", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The tests' image of RT-11 programs: the system with /rt11 holding the
 * GCC toolset's examples and SABOT2 when the build found them; empty when
 * it was not built. */
inline std::vector<uint8_t> readRt11Image()
{
    std::ifstream in(imagePath().parent_path() / "rt11test.dsk", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/* The byte offset of block lbn of a double-sided image: side 0 for blocks
 * 0..799, side 1 after; cylinder 0 holds a side's last ten blocks, the
 * sectors 2:1 interleaved and skewed by two a track (docs/hardware/
 * filesystem.md).  A single-sided image has side 0's tracks alone, one
 * after another. */
inline std::size_t blockOffset(int lbn, bool singleSided = false)
{
    static const int interleave[10] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 9};
    const int side = lbn / 800, n = lbn % 800;
    const int track = (n / 10 + 1) % 80;
    const int sector = (interleave[n % 10] + 2 * track - 2 + 20) % 10;
    const std::size_t stride = singleSided ? 5120 : 10240;
    return static_cast<std::size_t>(side) * 5120 + static_cast<std::size_t>(track) * stride +
           static_cast<std::size_t>(sector) * 512;
}

class Machine {
public:
    /* The image as it is, or changed: written to a file of its own; with
     * ramDisk the machine has the EX board's 512 KB RAM disk. */
    explicit Machine(const std::vector<uint8_t> &image, bool ramDisk = false)
    {
        if (ramDisk) emu_.enableRamDisk();
        path_ = fs::temp_directory_path() / ("unix_test_" + std::to_string(counter()++) + ".dsk");
        std::ofstream(path_, std::ios::binary)
            .write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        REQUIRE(emu_.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-romb.rom"));
        REQUIRE(emu_.mountDisk(0, path_.string()));
        if (image.size() == 819200) REQUIRE(emu_.mountDisk(2, path_.string()));
        emu_.reset();
    }
    ~Machine()
    {
        emu_.unmountDisk(0);
        emu_.unmountDisk(2);
        std::error_code ec;
        fs::remove(path_, ec);
        if (!second_.empty()) {
            emu_.unmountDisk(1);
            emu_.unmountDisk(3);
            fs::remove(second_, ec);
        }
    }

    /* A diskette put in the second drive: FD1 by the emulator's units
     * (side * 2 + drive), and FD3 too, its side 1, when it is double-sided
     * - an 819200-byte image, as the boot drive's FD0 and FD2. */
    void insertSecond(const std::vector<uint8_t> &image)
    {
        second_ = fs::temp_directory_path() / ("unix_test_" + std::to_string(counter()++) + ".dsk");
        std::ofstream(second_, std::ios::binary)
            .write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        REQUIRE(emu_.mountDisk(1, second_.string()));
        if (image.size() == 819200) REQUIRE(emu_.mountDisk(3, second_.string()));
    }

    /* The second drive's diskette as it stands now. */
    std::vector<uint8_t> secondNow() const
    {
        std::ifstream in(second_, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    Machine(const Machine &) = delete;
    Machine &operator=(const Machine &) = delete;

    /* Frames run, the keys queued with type() pressed a frame at a time. */
    void run(int frames)
    {
        for (int i = 0; i < frames; ++i) {
            typist_.pump(emu_);
            (void)emu_.stepFrame();
        }
    }

    /* Characters typed as a person would: queued, then pressed by run().
     * The Typist knows the ROM's keyboard, where a bare Latin key gives the
     * capital; the system turns that round, a bare key small, so a letter
     * goes to the Typist in the other case to come out in its own. */
    void type(const std::string &text)
    {
        for (char c : text) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            else if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            typist_.type(static_cast<uint8_t>(c));
        }
    }

    /* A command line typed at the shell's prompt, and frames run until the
     * prompt comes back - the next line is not typed into the last one's
     * output - at most `limit`. */
    bool command(const std::string &line, int limit = 1500)
    {
        typeAndSettle(line + "\r", 0);
        return waitPrompt(limit);
    }

    /* The shell's prompt on the screen: its last line is "$" alone - while a
     * command runs it is the line typed, or the command's output.  Frames
     * run until it is, at most `limit`. */
    bool waitPrompt(int limit = 1500)
    {
        auto prompt = [this] {
            std::string s = screen();
            while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
            const auto nl = s.rfind('\n');
            return s.substr(nl == std::string::npos ? 0 : nl + 1) == "$";
        };
        for (int f = 0; f < limit; f += 5) {
            run(5);
            if (prompt()) {
                run(5);
                return true;
            }
        }
        return false;
    }

    /* A key pressed bare: what the keyboard gives without Shift. */
    void tapBare(ms0515::Key key) { typist_.type(key); }

    /* The text typed, frames run until its last key is let go, then
     * `frames` more. */
    void typeAndSettle(const std::string &text, int frames)
    {
        type(text);
        for (int i = 0; i < 4000 && typist_.pending() > 0; ++i) run(1);
        run(frames);
    }

    /* The screen's text, a line a row. */
    std::string screen()
    {
        ms0515::Terminal term;
        auto snap = term.decode(emu_);
        std::string text;
        for (int r = 0; r < ms0515::Terminal::kRows; ++r) text += snap.row(r) + "\n";
        return text;
    }

    /* Frames run until the screen shows `what`, at most `limit`. */
    bool runUntil(const std::string &what, int limit)
    {
        for (int f = 0; f < limit; f += 10) {
            run(10);
            if (screen().find(what) != std::string::npos) return true;
        }
        return false;
    }

    ms0515::Emulator &emulator() { return emu_; }

    /* The RAM disk's 512 KB as they stand, empty when there is no board. */
    std::vector<uint8_t> ramDisk()
    {
        const auto &rd = ms0515::internal::board(emu_).ramdisk;
        if (!rd.enabled || !rd.ram) return {};
        return {rd.ram, rd.ram + 512 * 1024};
    }

    /* The diskette as it stands now: the emulator writes a sector through
     * to the image file as the machine writes it. */
    std::vector<uint8_t> imageNow() const
    {
        std::ifstream in(path_, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

private:
    static int &counter()
    {
        static int n = 0;
        return n;
    }
    ms0515::Emulator emu_;
    ms0515::Typist typist_;
    fs::path path_;
    fs::path second_;
};

} // namespace unixos
