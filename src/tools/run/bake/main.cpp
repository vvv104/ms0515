/*
 * ms0515-run-bake - makes what ms0515-run carries inside: a build step,
 * not a shipped tool.
 *
 *     ms0515-run-bake ROM SYSTEM.dsk OUT.cpp
 *     ms0515-run-bake ROM COLLECTION OUT.cpp
 *
 * SYSTEM.dsk is the dec system diskette as `ms0515-disk compose` makes
 * it; a directory instead is the software collection, and the diskette
 * is composed of it here, the same one (the browser build has no
 * ms0515-disk to run).  The startup file is taken off it and the starter put on; the
 * machine is booted from it, the date set, HD loaded and made DK:, the
 * instruction emulator EM fitted to this monitor (and left off), and the
 * starter run.  With the starter waiting on a cleared screen the state
 * is saved; the diskette is taken as the monitor left it, so that
 * the two agree, and as a sparse volume - the blocks in use, not an
 * image of the medium.  ROM, volume and state go into OUT.cpp packed
 * (ZeroRun.hpp) as the arrays Embedded.hpp declares.
 */

#include "Starter.hpp"
#include "ZeroRun.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Manifest.hpp>

#include <fmt/format.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ms0515::Emulator;
using ms0515::Key;

/* The date every run starts at and every file a program writes gets: the
 * dec system's own startup date.  RT-11 V5.04 shows years past 1999 wrong. */
constexpr const char *kDateCommand = "DATE 31-DEC-99";
constexpr const char *kDateShown   = "31-Dec-99";

constexpr int kBootFrames    = 4000;
constexpr int kCommandFrames = 1500;

std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    return {std::istreambuf_iterator<char>(in), {}};
}

/* The dec system diskette composed of the collection at `root`: what
 * `ms0515-disk compose --system dec --media ss --add hd-rt11 --add em`
 * writes - the monitor, the swap file, the three handlers and EM. */
std::vector<uint8_t> composedSystem(const std::filesystem::path &root)
{
    namespace fs = std::filesystem;
    namespace disk = ms0515::disk;
    const auto text = readFile((root / "disks.toml").string());
    const disk::Manifest manifest =
        disk::parseManifest(std::string(text.begin(), text.end()));
    disk::Repository repo;
    for (auto it = fs::recursive_directory_iterator(root);
         it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_directory() && it->path().filename() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file())
            repo.paths.push_back(it->path().lexically_relative(root).generic_string());
    }
    repo.read = [root](const std::string &path) -> std::optional<std::vector<uint8_t>> {
        std::ifstream in(root / fs::path(path), std::ios::binary);
        if (!in) return std::nullopt;
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), {}};
    };
    disk::Selection selection;
    selection.system = "dec";
    selection.media = disk::Media::ss;
    selection.bundles = {"hd-rt11", "em"};
    return disk::composeDisk(disk::recipeFor(manifest, selection, repo));
}

std::vector<std::string> screenRows(const Emulator &emu)
{
    ms0515::Terminal term;
    const auto snap = term.decode(emu);
    std::vector<std::string> rows;
    for (int r = 0; r < ms0515::Terminal::kRows; ++r) {
        auto row = snap.row(r);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string screenText(const Emulator &emu)
{
    std::string text;
    for (const auto &row : screenRows(emu))
        if (!row.empty()) text += "    " + row + "\n";
    return text;
}

/* The monitor's dot on the last line in use, with the cursor or not. */
bool atPrompt(const Emulator &emu)
{
    const auto rows = screenRows(emu);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        if (!it->empty()) return *it == "." || *it == "._";
    return false;
}

bool screenShows(const Emulator &emu, std::string_view text)
{
    for (const auto &row : screenRows(emu))
        if (row.find(text) != std::string::npos) return true;
    return false;
}

void stepFrames(Emulator &emu, int n)
{
    for (int i = 0; i < n; ++i)
        (void)emu.stepFrame();
}

void waitPrompt(Emulator &emu, int frames, std::string_view what)
{
    for (int f = 0; f < frames; f += 5) {
        stepFrames(emu, 5);
        if (atPrompt(emu)) return;
    }
    throw std::runtime_error(fmt::format("no monitor prompt after {}:\n{}",
                                         what, screenText(emu)));
}

Key keyOf(char c)
{
    static constexpr Key letters[26] = {
        Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G, Key::H, Key::I,
        Key::J, Key::K, Key::L, Key::M, Key::N, Key::O, Key::P, Key::Q, Key::R,
        Key::S, Key::T, Key::U, Key::V, Key::W, Key::X, Key::Y, Key::Z,
    };
    static constexpr Key digits[10] = {
        Key::Digit0, Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4,
        Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9,
    };
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
    case ' ': return Key::Space;
    case '-': return Key::MinusEq;
    case ':': return Key::ColonStar;
    case '.': return Key::Period;
    default:  throw std::runtime_error(fmt::format("no key for '{}'", c));
    }
}

void typeLine(Emulator &emu, std::string_view line)
{
    const auto press = [&](Key key) {
        emu.keyPress(key, true);
        stepFrames(emu, 2);
        emu.keyPress(key, false);
        stepFrames(emu, 4);
    };
    for (char c : line) press(keyOf(c));
    press(Key::Return);
}

/* A command the monitor takes without complaint: nothing of its answer
 * (the lines under the command's echo) starts with the `?` of an error. */
void command(Emulator &emu, std::string_view line)
{
    typeLine(emu, line);
    waitPrompt(emu, kCommandFrames, line);
    const auto rows = screenRows(emu);
    std::size_t echo = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].find(line) != std::string::npos) echo = i;
    bool refused = echo == rows.size();
    for (std::size_t i = echo; i < rows.size(); ++i)
        if (!rows[i].empty() && rows[i].front() == '?') refused = true;
    if (refused)
        throw std::runtime_error(fmt::format("the monitor refused {}:\n{}",
                                             line, screenText(emu)));
}

/* The diskette the machine boots: the composed one, less its startup
 * file (the commands are typed here instead), plus the starter. */
std::vector<uint8_t> systemDiskette(std::vector<uint8_t> image)
{
    namespace disk = ms0515::disk;
    if (image.size() != ms0515::kFloppyDiskSize)
        throw std::runtime_error("the system diskette is not a single-sided image");
    const auto opened = disk::openImage(image);
    if (!opened)
        throw std::runtime_error("the system diskette has no RT-11 directory");
    if (opened->directory.find("STARTS.COM"))
        disk::removeFile(image, 0, false, "STARTS.COM");
    const auto starter = ms0515::run::starterProgram();
    disk::PutOptions opts;
    opts.date = disk::encodeDate(1999, 12, 31);
    disk::putFile(image, 0, false, ms0515::run::kStarterFile, starter, opts);
    return image;
}

/* Run the starter: the screen blank (the cursor in the dark half of its
 * blink) and the program in its wait. */
void runStarter(Emulator &emu)
{
    typeLine(emu, std::string{"RUN SY:"} + "START");
    for (int f = 0; f < kCommandFrames; ++f) {
        stepFrames(emu, 1);
        bool blank = true;
        for (const auto &row : screenRows(emu))
            if (!row.empty()) { blank = false; break; }
        if (blank && ms0515::run::starterWaiting(emu) &&
            !ms0515::run::monitorInControl(emu))
            return;
    }
    throw std::runtime_error("the starter did not come to its wait:\n" +
                             screenText(emu));
}

void writeArray(std::string &out, const char *name, std::span<const uint8_t> raw)
{
    const auto packed = ms0515::run::packZeroRuns(raw);
    out += fmt::format("const std::uint8_t {}Bytes[] = {{", name);
    for (std::size_t i = 0; i < packed.size(); ++i)
        out += fmt::format("{}{},", i % 20 == 0 ? "\n    " : "", packed[i]);
    out += "\n};\n\n";
    fmt::print("  {:<6} {:>7} bytes, {:>7} packed\n", name, raw.size(),
               packed.size());
}

void bake(const std::string &romPath, const std::string &diskPath,
          const std::string &outPath)
{
    const auto rom = readFile(romPath);
    Emulator emu;
    emu.loadRom(rom);
    auto system = std::filesystem::is_directory(diskPath) ? composedSystem(diskPath)
                                                           : readFile(diskPath);
    if (!emu.mountDiskImage(0, systemDiskette(std::move(system))))
        throw std::runtime_error("cannot mount the system diskette");
    emu.setHdEnabled(true);
    emu.reset();

    waitPrompt(emu, kBootFrames, "the boot");
    command(emu, "SET TT QUIET");
    command(emu, kDateCommand);
    command(emu, "DATE");
    if (!screenShows(emu, kDateShown))
        throw std::runtime_error("the date did not take:\n" + screenText(emu));
    command(emu, "LOAD HD");
    command(emu, "ASSIGN HD DK");
    /* EM, the emulator of the instructions the processor has not (MUL,
     * DIV, ASH, ASHC, the FIS four): fitted to this monitor here, which
     * writes into EM.SYS on the diskette, and left off - resident it
     * takes memory, so the tool switches it on (SET EM ON) only for a
     * run that asks for it. */
    command(emu, "SET EM SYSGEN");
    runStarter(emu);

    std::vector<uint8_t> state;
    if (auto r = emu.saveState(state); !r)
        throw std::runtime_error(r.error());

    std::string text = "/* Generated by ms0515-run-bake - do not edit. */\n\n"
                       "#include \"Embedded.hpp\"\n\nnamespace {\n\n";
    writeArray(text, "rom", rom);
    /* The diskette as the monitor left it, as a sparse volume: the blocks
     * its files and directory use, none of the image's free space. */
    const auto mounted = emu.diskImage(0);
    const auto left = ms0515::disk::openImage({mounted.begin(), mounted.end()});
    if (!left)
        throw std::runtime_error("the diskette the monitor left has no directory");
    const auto volume = ms0515::disk::SparseVolume::fromImage(*left);
    fmt::print("  the system volume holds {} of its {} blocks\n", volume.held(),
               volume.blocks());
    writeArray(text, "disk", volume.serialize());
    writeArray(text, "state", state);
    text += "} /* namespace */\n\nnamespace ms0515::run::embedded {\n\n"
            "const std::span<const std::uint8_t> rom{romBytes};\n"
            "const std::span<const std::uint8_t> disk{diskBytes};\n"
            "const std::span<const std::uint8_t> state{stateBytes};\n\n"
            "} /* namespace ms0515::run::embedded */\n";

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out)
        throw std::runtime_error("cannot write " + outPath);
}

} /* namespace */

int main(int argc, char **argv)
{
    if (argc != 4) {
        fmt::print(stderr, "usage: ms0515-run-bake ROM SYSTEM.dsk|COLLECTION OUT.cpp\n");
        return 2;
    }
    try {
        bake(argv[1], argv[2], argv[3]);
    } catch (const std::exception &e) {
        fmt::print(stderr, "ms0515-run-bake: {}\n", e.what());
        return 1;
    }
    return 0;
}
