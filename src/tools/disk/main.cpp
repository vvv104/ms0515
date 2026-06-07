/*
 * ms0515-disk — offline RT-11 / MS-0515 disk utility.
 *
 * Commands mirror the OS workflow:
 *   create <out.dsk> [--ds]                 raw blank media (no filesystem)
 *   init   <image> [--side N] [opts]        format a side (like INITIALIZE)
 *   put    <image> [--side N] [--date YYYY-MM-DD] [--protected] <file>...
 *                                           add host files (like PIP, in)
 *   rm     <image> [--side N] <name>...     delete files
 *   squeeze <image> [--side N]              defragment (RT-11 SQUEEZE)
 *   protect/unprotect <image> [--side N] <name>...  toggle the /PROTECT flag
 *   setdate <image> [--side N] --date YYYY-MM-DD <name>...
 *                                           write the directory date in place
 *   get    <image> [--side N] [--out D] [pat]...  extract files (like PIP, out)
 *   dir    <image> [--side N]               list the directory
 *
 * The geometry follows from the image size (409600 = single-sided, 819200 =
 * double-sided) and matches the emulator FDC.  Wildcards use '*' only.
 *
 * Format-level operations only.  Heuristic multi-source recovery lives in
 * Python under disk_recovery/, on top of these primitives.
 */

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace ms0515::disk;

namespace {

int usage()
{
    std::fputs(
        "usage: ms0515-disk <command> [args]\n"
        "  create <out.dsk> [--ds]               raw blank media\n"
        "  init   <image> [--side 0|1] [--volume-id ID] [--owner NAME] [--segments N]\n"
        "                                        format a side (empty volume)\n"
        "  put    <image> [--side 0|1] [--date YYYY-MM-DD] [--protected] <file|glob>...\n"
        "                                        add host files (PIP-style)\n"
        "  rm     <image> [--side 0|1] <name>...   delete files (frees the blocks)\n"
        "  squeeze <image> [--side 0|1]          defragment (RT-11 SQUEEZE)\n"
        "  protect/unprotect <image> [--side 0|1] <name>...   set/clear /PROTECT\n"
        "  setdate <image> [--side 0|1] --date YYYY-MM-DD <name>...\n"
        "                                        write the directory date in place\n"
        "  get    <image> [--side 0|1] [--out DIR] [pattern]...  extract files\n"
        "  dir    <image> [--side 0|1]           list the directory\n"
        "  split  <ds.dsk> <side0.dsk> <side1.dsk>   split an 800 KB DS into two 400 KB SS\n"
        "  merge  <side0.dsk> <side1.dsk> <ds.dsk>   merge two 400 KB SS into an 800 KB DS\n"
        "\n"
        "  Image kind follows the size: 409600 B single-sided, 819200 B double-\n"
        "  sided (--side picks a side, default 0 = lower/boot).  Wildcards: '*'.\n",
        stderr);
    return 2;
}

std::optional<std::vector<uint8_t>> readHostFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

bool writeWholeFile(const std::string &path, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

/* '*'-only glob, case-insensitive (RT-11 names are upper-case). */
bool globMatch(std::string_view pat, std::string_view s)
{
    auto up = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; };
    std::size_t pi = 0, si = 0, star = std::string_view::npos, mark = 0;
    while (si < s.size()) {
        if (pi < pat.size() && pat[pi] == '*') { star = pi++; mark = si; }
        else if (pi < pat.size() && up(pat[pi]) == up(s[si])) { ++pi; ++si; }
        else if (star != std::string_view::npos) { pi = star + 1; si = ++mark; }
        else return false;
    }
    while (pi < pat.size() && pat[pi] == '*') ++pi;
    return pi == pat.size();
}

/* Read an existing image to modify in place; validate its size. */
std::optional<std::vector<uint8_t>> readImage(const std::string &path, bool &ds)
{
    auto raw = readHostFile(path);
    if (!raw) { std::fprintf(stderr, "error: cannot read %s\n", path.c_str()); return std::nullopt; }
    if (raw->size() != kSideSize && raw->size() != kDoubleSize) {
        std::fprintf(stderr, "error: %s is %zu B, not a 400 KB or 800 KB image\n",
                     path.c_str(), raw->size());
        return std::nullopt;
    }
    ds = isDoubleSidedSize(raw->size());
    return raw;
}

int cmdDir(const std::string &path, int side)
{
    auto img = loadImage(path, side);
    if (!img) { std::fprintf(stderr, "error: cannot read %s (bad --side?)\n",
                             path.c_str()); return 1; }
    std::printf("%s\n  size %zu B  (%s, side %d)\n", path.c_str(), img->data.size(),
                img->ds ? "double-sided" : "single-sided", img->side);
    if (!img->hasDirectory) { std::printf("  no RT-11 directory on this side\n"); return 1; }
    const auto &d = img->directory;
    std::printf("  dir@LBN %d  segs=%d  data_start=%d\n",
                d.dirStartLbn, d.segsTotal, d.dataStart);
    int n = 0;
    for (const auto &e : d.entries)
        if (e.isPermanent()) {
            const auto dp = decodeDate(e.date);
            char dateBuf[16] = "       -  ";
            if (dp.year)
                std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                              dp.year, dp.month, dp.day);
            const bool prot = (e.status & kStatusProtected) != 0;
            std::printf("    %-14s blk=%5d  len=%5d blocks  (%6d B)  date=%s%s\n",
                        e.name.c_str(), e.startBlock, e.length, e.length * kBlock,
                        dateBuf, prot ? "  [P]" : "");
            ++n;
        }
    std::printf("  %d permanent file(s)\n", n);
    return 0;
}

int cmdGet(const std::string &path, int side, const std::string &outdir,
           const std::vector<std::string> &patterns)
{
    auto img = loadImage(path, side);
    if (!img) { std::fprintf(stderr, "error: cannot read %s (bad --side?)\n",
                             path.c_str()); return 1; }
    if (!img->hasDirectory) {
        std::fprintf(stderr, "error: no RT-11 directory on side %d of %s\n",
                     img->side, path.c_str());
        return 1;
    }
    std::error_code ec;
    fs::create_directories(outdir, ec);

    auto matches = [&](const std::string &name) {
        if (patterns.empty()) return true;            /* no pattern => all */
        for (const auto &p : patterns) if (globMatch(p, name)) return true;
        return false;
    };

    int got = 0, fails = 0;
    for (const auto &e : img->directory.entries) {
        if (!e.isPermanent() || !matches(e.name)) continue;
        auto bytes = img->readFile(e.name);
        const fs::path out = fs::path(outdir) / e.name;
        if (!writeWholeFile(out.string(), bytes)) {
            std::fprintf(stderr, "error: cannot write %s\n", out.string().c_str());
            ++fails; continue;
        }
        std::printf("  %-14s -> %s (%zu B)\n", e.name.c_str(),
                    out.string().c_str(), bytes.size());
        ++got;
    }
    if (got == 0) std::fprintf(stderr, "warning: no files matched\n");
    return fails ? 1 : 0;
}

int cmdInit(const std::string &path, int side, const InitOptions &opts)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;
    try {
        initVolume(*image, side, ds, opts);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    if (!writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    std::printf("initialised %s (side %d, %s)\n", path.c_str(), side,
                ds ? "double-sided" : "single-sided");
    return 0;
}

std::optional<uint16_t> parseDate(const std::string &s)
{
    /* "YYYY-MM-DD" only; reject anything else.  We hand the components to
     * encodeDate(), which enforces the RT-11 range (1972..2099). */
    int parts[3]{};
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t end = (i < 2) ? s.find('-', start) : s.size();
        if (end == std::string::npos || end == start) return std::nullopt;
        const char *p = s.data() + start;
        auto [ptr, ec] = std::from_chars(p, s.data() + end, parts[i]);
        if (ec != std::errc{} || ptr != s.data() + end) return std::nullopt;
        start = end + 1;
    }
    try { return encodeDate(parts[0], parts[1], parts[2]); }
    catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return std::nullopt;
    }
}

int cmdPut(const std::string &path, int side, const std::vector<std::string> &args,
           const PutOptions &opts)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;

    /* Expand '*' globs against the host filesystem; plain paths pass through. */
    std::vector<fs::path> hostFiles;
    for (const auto &a : args) {
        if (a.find('*') == std::string::npos) { hostFiles.emplace_back(a); continue; }
        const fs::path pat(a);
        const fs::path dir = pat.has_parent_path() ? pat.parent_path() : fs::path(".");
        const std::string fpat = pat.filename().string();
        std::error_code ec;
        bool any = false;
        if (fs::is_directory(dir, ec))
            for (const auto &de : fs::directory_iterator(dir, ec))
                if (de.is_regular_file(ec) &&
                    globMatch(fpat, de.path().filename().string())) {
                    hostFiles.push_back(de.path()); any = true;
                }
        if (!any) std::fprintf(stderr, "warning: no host files match %s\n", a.c_str());
    }
    if (hostFiles.empty()) { std::fprintf(stderr, "error: no input files\n"); return 1; }

    int added = 0, fails = 0;
    for (const auto &hf : hostFiles) {
        auto bytes = readHostFile(hf.string());
        if (!bytes) { std::fprintf(stderr, "error: cannot read %s\n", hf.string().c_str());
                      ++fails; continue; }
        const std::string name = hf.filename().string();
        try {
            putFile(*image, side, ds, name, *bytes, opts);
            std::printf("  %s -> %s (%zu B)\n", hf.string().c_str(), name.c_str(),
                        bytes->size());
            ++added;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            ++fails;
        }
    }
    if (added && !writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    return fails ? 1 : 0;
}

int cmdRm(const std::string &path, int side, const std::vector<std::string> &names)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;

    int removed = 0, fails = 0;
    for (const auto &name : names) {
        try {
            removeFile(*image, side, ds, name);
            std::printf("  removed %s\n", name.c_str());
            ++removed;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            ++fails;
        }
    }
    if (removed && !writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    return fails ? 1 : 0;
}

int cmdSqueeze(const std::string &path, int side)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;
    try { squeeze(*image, side, ds); }
    catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    if (!writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    std::printf("  squeezed %s (side %d)\n", path.c_str(), side);
    return 0;
}

int cmdSetdate(const std::string &path, int side, uint16_t date,
               const std::vector<std::string> &names)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;

    int changed = 0, fails = 0;
    for (const auto &name : names) {
        try {
            setEntryDate(*image, side, ds, name, date);
            std::printf("  dated %s\n", name.c_str());
            ++changed;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            ++fails;
        }
    }
    if (changed && !writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    return fails ? 1 : 0;
}

int cmdProtect(const std::string &path, int side,
               const std::vector<std::string> &names, bool on)
{
    bool ds = false;
    auto image = readImage(path, ds);
    if (!image) return 1;

    int changed = 0, fails = 0;
    for (const auto &name : names) {
        try {
            setProtected(*image, side, ds, name, on);
            std::printf("  %sprotected %s\n", on ? "" : "un", name.c_str());
            ++changed;
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            ++fails;
        }
    }
    if (changed && !writeWholeFile(path, *image)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 1;
    }
    return fails ? 1 : 0;
}

}  /* namespace */

int main(int argc, char **argv)
{
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "create") {
        std::string out; bool ds = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--ds") { ds = true; continue; }
            if (out.empty()) { out = std::string(a); continue; }
            return usage();
        }
        if (out.empty()) return usage();
        auto image = blankImage(ds);
        if (!writeWholeFile(out, image)) {
            std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
        std::printf("created %s (%zu B, %s, unformatted)\n", out.c_str(),
                    image.size(), ds ? "double-sided" : "single-sided");
        return 0;
    }

    if (cmd == "init") {
        std::string image; int side = 0; InitOptions opts;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if      (a == "--side"      && i + 1 < argc) side = std::atoi(argv[++i]);
            else if (a == "--volume-id" && i + 1 < argc) opts.volumeId = argv[++i];
            else if (a == "--owner"     && i + 1 < argc) opts.owner = argv[++i];
            else if (a == "--segments"  && i + 1 < argc) opts.segments = std::atoi(argv[++i]);
            else if (image.empty()) image = std::string(a);
            else return usage();
        }
        if (image.empty()) return usage();
        return cmdInit(image, side, opts);
    }

    if (cmd == "put") {
        std::string image; int side = 0; std::vector<std::string> files;
        PutOptions opts;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (a == "--protected") { opts.readOnly = true; continue; }
            if (a == "--date" && i + 1 < argc) {
                auto d = parseDate(argv[++i]);
                if (!d) { std::fprintf(stderr, "error: --date wants YYYY-MM-DD\n"); return 2; }
                opts.date = *d; continue;
            }
            if (image.empty()) { image = std::string(a); continue; }
            files.emplace_back(a);
        }
        if (image.empty() || files.empty()) return usage();
        return cmdPut(image, side, files, opts);
    }

    if (cmd == "rm") {
        std::string image; int side = 0; std::vector<std::string> names;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (image.empty()) { image = std::string(a); continue; }
            names.emplace_back(a);
        }
        if (image.empty() || names.empty()) return usage();
        return cmdRm(image, side, names);
    }

    if (cmd == "squeeze") {
        std::string image; int side = 0;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (image.empty()) { image = std::string(a); continue; }
            return usage();
        }
        if (image.empty()) return usage();
        return cmdSqueeze(image, side);
    }

    if (cmd == "setdate") {
        std::string image, dateStr; int side = 0; std::vector<std::string> names;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (a == "--date" && i + 1 < argc) { dateStr = argv[++i]; continue; }
            if (image.empty()) { image = std::string(a); continue; }
            names.emplace_back(a);
        }
        if (image.empty() || names.empty() || dateStr.empty()) return usage();
        auto d = parseDate(dateStr);
        if (!d) { std::fprintf(stderr, "error: --date wants YYYY-MM-DD\n"); return 2; }
        return cmdSetdate(image, side, *d, names);
    }

    if (cmd == "protect" || cmd == "unprotect") {
        std::string image; int side = 0; std::vector<std::string> names;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (image.empty()) { image = std::string(a); continue; }
            names.emplace_back(a);
        }
        if (image.empty() || names.empty()) return usage();
        return cmdProtect(image, side, names, cmd == "protect");
    }

    if (cmd == "get") {
        std::string image, outdir = "."; int side = 0; std::vector<std::string> pats;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (a == "--out"  && i + 1 < argc) { outdir = argv[++i]; continue; }
            if (image.empty()) { image = std::string(a); continue; }
            pats.emplace_back(a);
        }
        if (image.empty()) return usage();
        return cmdGet(image, side, outdir, pats);
    }

    if (cmd == "dir") {
        std::string image; int side = 0;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            if (image.empty()) { image = std::string(a); continue; }
            return usage();
        }
        if (image.empty()) return usage();
        return cmdDir(image, side);
    }

    if (cmd == "split") {
        if (argc != 5) return usage();
        auto ds = readHostFile(argv[2]);
        if (!ds) { std::fprintf(stderr, "error: cannot read %s\n", argv[2]); return 1; }
        auto sides = splitDoubleSided(*ds);
        if (!sides) { std::fprintf(stderr, "error: %s is not an 819200-byte DS image\n",
                                   argv[2]); return 1; }
        if (!writeWholeFile(argv[3], sides->first) ||
            !writeWholeFile(argv[4], sides->second)) {
            std::fprintf(stderr, "error: cannot write output\n"); return 1; }
        std::printf("split %s -> %s (side 0) + %s (side 1)\n", argv[2], argv[3], argv[4]);
        return 0;
    }

    if (cmd == "merge") {
        if (argc != 5) return usage();
        auto s0 = readHostFile(argv[2]);
        auto s1 = readHostFile(argv[3]);
        if (!s0) { std::fprintf(stderr, "error: cannot read %s\n", argv[2]); return 1; }
        if (!s1) { std::fprintf(stderr, "error: cannot read %s\n", argv[3]); return 1; }
        auto ds = mergeSides(*s0, *s1);
        if (!ds) { std::fprintf(stderr, "error: both inputs must be 409600-byte SS images\n");
                   return 1; }
        if (!writeWholeFile(argv[4], *ds)) {
            std::fprintf(stderr, "error: cannot write %s\n", argv[4]); return 1; }
        std::printf("merged %s + %s -> %s\n", argv[2], argv[3], argv[4]);
        return 0;
    }

    return usage();
}
