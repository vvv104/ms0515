/*
 * ms0515-disk — offline RT-11 / MS-0515 disk utility.
 *
 * Subcommands:
 *   dir     <image> [--side N]      list a capture's directory
 *   extract <image> [--side N] [name] [outdir]  extract one file (or all)
 *   build   <out.dsk> [--ds] <file>...          assemble files into a volume
 *
 * There is no layout option: the geometry follows from the image size
 * (409600 = single-sided, 819200 = double-sided) and matches the emulator
 * FDC exactly.  --side picks a side of an 800 KB dump.
 *
 * Format-level operations only.  The heuristic multi-source recovery
 * (consensus, donor matching, confidence tiers) lives in Python under
 * disk_recovery/, on top of these primitives.
 */

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
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
        "  dir     <image> [--side 0|1]          list the directory\n"
        "  extract <image> [--side 0|1] [name] [outdir]\n"
        "                                        extract one file, or all\n"
        "  build   <out.dsk> [--ds] <file>...    assemble files into a volume\n"
        "\n"
        "  Two image kinds, by file size: 409600 B (400 KB) single-sided,\n"
        "  819200 B (800 KB) double-sided.  The physical layout follows from\n"
        "  the size (matching the emulator FDC), so there is no layout option.\n"
        "  --side selects a side of an 800 KB dump (0 = lower/boot, 1 = upper).\n"
        "  build writes 400 KB unless --ds is given.\n",
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

bool writeFile(const fs::path &out, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(out, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

int cmdDir(const std::string &path, int side)
{
    auto img = loadImage(path, side);
    if (!img) { std::fprintf(stderr, "error: cannot read %s (bad --side?)\n",
                             path.c_str()); return 1; }
    std::printf("%s\n  size %zu B  (%s, side %d)\n", path.c_str(), img->data.size(),
                img->ds ? "double-sided" : "single-sided", img->side);
    if (!img->hasDirectory) {
        std::printf("  no RT-11 directory on this side\n");
        return 1;
    }
    const auto &d = img->directory;
    std::printf("  dir@LBN %d  segs=%d  data_start=%d\n",
                d.dirStartLbn, d.segsTotal, d.dataStart);
    int n = 0;
    for (const auto &e : d.entries) {
        if (e.isPermanent()) {
            std::printf("    %-14s blk=%5d  len=%5d blocks  (%d B)\n",
                        e.name.c_str(), e.startBlock, e.length, e.length * kBlock);
            ++n;
        }
    }
    std::printf("  %d permanent file(s)\n", n);
    return 0;
}

int cmdExtract(const std::string &path, int side,
               std::string_view name, const std::string &outdir)
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

    auto dump = [&](const DirEntry &e) -> bool {
        auto bytes = img->readFile(e.name);
        const fs::path out = fs::path(outdir) / e.name;
        if (!writeFile(out, bytes)) {
            std::fprintf(stderr, "error: cannot write %s\n", out.string().c_str());
            return false;
        }
        std::printf("  %-14s -> %s (%zu B)\n", e.name.c_str(),
                    out.string().c_str(), bytes.size());
        return true;
    };

    if (!name.empty()) {
        const DirEntry *e = img->directory.find(name);
        if (!e) { std::fprintf(stderr, "error: %.*s not found\n",
                               static_cast<int>(name.size()), name.data()); return 1; }
        return dump(*e) ? 0 : 1;
    }
    int fails = 0;
    for (const auto &e : img->directory.entries)
        if (e.isPermanent() && !dump(e)) ++fails;
    return fails ? 1 : 0;
}

}  /* namespace */

int main(int argc, char **argv)
{
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "dir" || cmd == "extract") {
        std::string image, name, outdir = ".";
        int side = 0, positional = 0;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--side" && i + 1 < argc) { side = std::atoi(argv[++i]); continue; }
            switch (positional++) {
            case 0: image  = std::string(a); break;
            case 1: name   = std::string(a); break;   /* extract only */
            case 2: outdir = std::string(a); break;   /* extract only */
            default: return usage();
            }
        }
        if (image.empty()) return usage();
        return cmd == "dir" ? cmdDir(image, side)
                            : cmdExtract(image, side, name, outdir);
    }

    if (cmd == "build") {
        if (argc < 3) return usage();
        std::string out;
        bool ds = false;
        std::vector<BuildFile> files;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--ds") { ds = true; continue; }
            if (out.empty()) { out = std::string(a); continue; }
            auto bytes = readHostFile(std::string(a));
            if (!bytes) { std::fprintf(stderr, "error: cannot read %.*s\n",
                                       static_cast<int>(a.size()), a.data()); return 1; }
            files.push_back({fs::path(a).filename().string(), std::move(*bytes)});
        }
        if (out.empty() || files.empty()) return usage();
        try {
            auto image = ds ? buildDoubleSided(files, {})
                            : buildVolume(files);
            if (!writeFile(out, image)) {
                std::fprintf(stderr, "error: cannot write %s\n", out.c_str());
                return 1;
            }
            std::printf("wrote %s (%zu B, %zu files, %s)\n",
                        out.c_str(), image.size(), files.size(),
                        ds ? "double-sided" : "single-sided");
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
        return 0;
    }
    return usage();
}
