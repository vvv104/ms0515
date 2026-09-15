/*
 * Sounds.cpp - the recordings of the drive and the keyboard, read from the
 * assets tree.  See Sounds.hpp for the folder layout.
 */

#include "ms0515/app/Sounds.hpp"
#include "ms0515/app/Paths.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace ms0515::app {

namespace {

/* A WAV file's PCM, or an empty recording when the file is missing or not one. */
Pcm read(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto pcm = parseWav(bytes.data(), bytes.size());
    return pcm ? std::move(*pcm) : Pcm{};
}

/* "seek_31_68.wav" -> (31, 68): the track the head went from and the one it
   went to.  Anything else gives (-1, -1). */
std::pair<int, int> seekMove(const std::string &name)
{
    const std::string prefix = "seek_";
    if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - 4, 4, ".wav") != 0)
        return {-1, -1};
    const std::string body = name.substr(prefix.size(), name.size() - prefix.size() - 4);
    const std::size_t bar = body.find('_');
    if (bar == std::string::npos) return {-1, -1};
    int a = 0, b = 0;
    const char *f1 = body.data(), *l1 = body.data() + bar;
    const char *f2 = body.data() + bar + 1, *l2 = body.data() + body.size();
    const auto r1 = std::from_chars(f1, l1, a);
    const auto r2 = std::from_chars(f2, l2, b);
    if (r1.ec != std::errc{} || r1.ptr != l1 || r2.ec != std::errc{} || r2.ptr != l2)
        return {-1, -1};
    if (a < 0 || b < 0 || a == b) return {-1, -1};
    return {a, b};
}

/* "seek_in_12.wav" -> 12 for the prefix "seek_in_"; -1 for anything else. */
int seekLength(const std::string &name, const std::string &prefix)
{
    if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - 4, 4, ".wav") != 0)
        return -1;
    int n = 0;
    const char *first = name.data() + prefix.size();
    const char *last = name.data() + name.size() - 4;
    const auto r = std::from_chars(first, last, n);
    return r.ec == std::errc{} && r.ptr == last && n >= 0 ? n : -1;
}

fs::path findSet(const char *kind, const std::string &set)
{
    if (set.empty()) return {};
    for (const auto &root : Paths::searchRoots()) {
        const fs::path dir = root / "assets" / "sounds" / kind / set;
        std::error_code ec;
        if (fs::is_directory(dir, ec)) return dir;
    }
    return {};
}

} // namespace

std::vector<std::string> Sounds::driveSets()
{
    std::vector<std::string> sets;
    for (const auto &root : Paths::searchRoots()) {
        std::error_code ec;
        const fs::path dir = root / "assets" / "sounds" / "fdd";
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_directory(ec)) continue;
            const std::string name = entry.path().filename().string();
            if (std::find(sets.begin(), sets.end(), name) == sets.end()) sets.push_back(name);
        }
    }
    std::sort(sets.begin(), sets.end());
    return sets;
}

fs::path Sounds::driveDir(const std::string &set) { return findSet("fdd", set); }
fs::path Sounds::keyboardDir(const std::string &set) { return findSet("kbd", set); }

std::shared_ptr<DriveSounds> Sounds::loadDrive(const std::string &set)
{
    const fs::path dir = driveDir(set);
    return dir.empty() ? nullptr : loadDriveDir(dir);
}

std::shared_ptr<KeyboardSounds> Sounds::loadKeyboard(const std::string &set)
{
    const fs::path dir = keyboardDir(set);
    return dir.empty() ? nullptr : loadKeyboardDir(dir);
}

std::shared_ptr<DriveSounds> Sounds::loadDriveDir(const fs::path &dir)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return nullptr;
    auto s = std::make_shared<DriveSounds>();
    s->motorStart = read(dir / "motor_start.wav");
    s->motorLoop  = read(dir / "motor_loop.wav");
    s->motorStop  = read(dir / "motor_stop.wav");
    s->stepIn     = read(dir / "step_in.wav");
    s->stepOut    = read(dir / "step_out.wav");
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (const int track = seekLength(name, "step_"); track >= 0) {
            if (Pcm pcm = read(entry.path()); !pcm.empty()) s->steps[track] = std::move(pcm);
        } else if (const auto [a, b] = seekMove(name); a >= 0) {
            if (Pcm pcm = read(entry.path()); !pcm.empty())
                s->moves.push_back({a, b, std::move(pcm)});
        } else if (const int n = seekLength(name, "seek_in_"); n > 0) {
            if (Pcm pcm = read(entry.path()); !pcm.empty()) s->seekIn[n] = std::move(pcm);
        } else if (const int m = seekLength(name, "seek_out_"); m > 0) {
            if (Pcm pcm = read(entry.path()); !pcm.empty()) s->seekOut[m] = std::move(pcm);
        }
    }
    return s;
}

std::shared_ptr<KeyboardSounds> Sounds::loadKeyboardDir(const fs::path &dir)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return nullptr;
    auto s = std::make_shared<KeyboardSounds>();
    s->click = read(dir / "click.wav");
    s->bell  = read(dir / "bell.wav");
    return s;
}

} /* namespace ms0515::app */
