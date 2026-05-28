/*
 * ms0515-disk — offline RT-11 / MS-0515 disk utility.
 *
 * Subcommands:
 *   dir     <image>                 list a capture's directory
 *   extract <image> [name] [outdir] extract one file (or all) to outdir
 *   build   ...                     (not yet implemented)
 *
 * Format-level operations only.  The heuristic multi-source recovery
 * (consensus, donor matching, confidence tiers) lives in Python under
 * disk_recovery/, on top of these primitives.
 */

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
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
        "  dir     <image>                       list the directory\n"
        "  extract <image> [name] [outdir]       extract one file, or all\n"
        "  build   <out.dsk> [--layout <tag>] <file>...\n"
        "                                        assemble files into a volume\n"
        "          layout tags: ss-canonical (default), ss-osa-skew,\n"
        "          ss-cyl0last-noil, ss-cyl0first-noil, ss-lbn-linear,\n"
        "          ds-cyl0last-noil\n",
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

int cmdDir(const std::string &path)
{
    auto img = loadImage(path);
    if (!img) { std::fprintf(stderr, "error: cannot read %s\n", path.c_str()); return 1; }
    std::printf("%s\n  size %zu B\n", path.c_str(), img->data.size());
    if (!img->hasDirectory) {
        std::printf("  no RT-11 directory found under any candidate layout\n");
        return 1;
    }
    const auto &d = img->directory;
    std::printf("  layout=%.*s  dir@LBN %d  segs=%d  data_start=%d\n",
                static_cast<int>(layoutTag(img->layout).size()),
                layoutTag(img->layout).data(),
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

bool writeFile(const fs::path &out, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(out, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

int cmdExtract(const std::string &path, std::string_view name,
               const std::string &outdir)
{
    auto img = loadImage(path);
    if (!img || !img->hasDirectory) {
        std::fprintf(stderr, "error: no readable directory in %s\n", path.c_str());
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

    if (cmd == "dir") {
        if (argc < 3) return usage();
        return cmdDir(argv[2]);
    }
    if (cmd == "extract") {
        if (argc < 3) return usage();
        const std::string name   = argc >= 4 ? argv[3] : "";
        const std::string outdir = argc >= 5 ? argv[4] : ".";
        return cmdExtract(argv[2], name, outdir);
    }
    if (cmd == "build") {
        if (argc < 4) return usage();
        const std::string out = argv[2];
        Layout layout = Layout::SsCanonical;
        std::vector<BuildFile> files;
        for (int i = 3; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "--layout" && i + 1 < argc) {
                auto l = layoutFromTag(argv[++i]);
                if (!l) { std::fprintf(stderr, "error: unknown layout %s\n", argv[i]);
                          return 2; }
                layout = *l;
                continue;
            }
            auto bytes = readHostFile(std::string(a));
            if (!bytes) { std::fprintf(stderr, "error: cannot read %.*s\n",
                                       static_cast<int>(a.size()), a.data()); return 1; }
            files.push_back({fs::path(a).filename().string(), std::move(*bytes)});
        }
        if (files.empty()) return usage();
        try {
            auto image = buildVolume(layout, files);
            if (!writeFile(out, image)) {
                std::fprintf(stderr, "error: cannot write %s\n", out.c_str());
                return 1;
            }
            std::printf("wrote %s (%zu B, %d files, layout=%.*s)\n",
                        out.c_str(), image.size(), static_cast<int>(files.size()),
                        static_cast<int>(layoutTag(layout).size()),
                        layoutTag(layout).data());
        } catch (const std::exception &e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
        return 0;
    }
    return usage();
}
