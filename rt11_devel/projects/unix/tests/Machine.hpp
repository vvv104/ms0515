/*
 * Machine.hpp - the system's diskette booted on the native ROM-B, for the
 * tests.
 *
 * The image is the project's build (UNIX_DSK), or the file named with
 * --unix-dsk=<file>.  A double-sided image goes in as FD0 and FD2, its two
 * sides, as on the machine.
 */
#pragma once

#include <doctest/doctest.h>

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

/* The byte offset of block lbn of a double-sided image: side 0 for blocks
 * 0..799, side 1 after; cylinder 0 holds a side's last ten blocks, the
 * sectors 2:1 interleaved and skewed by two a track (docs/hardware/
 * filesystem.md). */
inline std::size_t blockOffset(int lbn)
{
    static const int interleave[10] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 9};
    const int side = lbn / 800, n = lbn % 800;
    const int track = (n / 10 + 1) % 80;
    const int sector = (interleave[n % 10] + 2 * track - 2 + 20) % 10;
    return static_cast<std::size_t>(side) * 5120 + static_cast<std::size_t>(track) * 10240 +
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
        /* The prompt back: the screen's last line is "$" alone - while the
         * command runs it is the line typed, or the command's output. */
        auto prompt = [this] {
            std::string s = screen();
            while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
            const auto nl = s.rfind('\n');
            return s.substr(nl == std::string::npos ? 0 : nl + 1) == "$";
        };
        typeAndSettle(line + "\r", 0);
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
};

} // namespace unixos
