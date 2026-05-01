#define _CRT_SECURE_NO_WARNINGS
#include "Assets.hpp"
#include "Config.hpp"   /* getExeDir */
#include "Video.hpp"

#include <SDL.h>

/* stb is header-only; we own the single .cpp that supplies the
 * implementation symbols. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>

namespace ms0515_frontend {

std::string findDefaultRom()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    /* 1. The SDL-supplied base path (the directory containing the
     *    executable).  Most reliable anchor on Windows. */
    std::vector<fs::path> roots;
    if (char *base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    /* 2. Current working directory. */
    roots.emplace_back(fs::current_path(ec));

    const char *rels[] = {
        "assets/rom/ms0515-roma.rom",
    };

    for (const auto &root : roots) {
        for (const char *rel : rels) {
            fs::path candidate = root / rel;
            if (fs::exists(candidate, ec))
                return candidate.lexically_normal().string();
        }
    }
    return {};
}

std::vector<std::string> discoverRoms()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<fs::path> roots;
    if (char *base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    roots.emplace_back(fs::current_path(ec));

    std::vector<std::string> result;
    for (const auto &root : roots) {
        fs::path romDir = root / "assets" / "rom";
        if (!fs::is_directory(romDir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(romDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".rom") continue;
            std::string path = entry.path().lexically_normal().string();
            if (std::find(result.begin(), result.end(), path) == result.end())
                result.push_back(path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string>
validateSingleSideImage(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return std::format("cannot stat '{}': {}", path, ec.message());
    if (sz == 409600)
        return std::nullopt;
    if (sz == 819200)
        return std::format(
            "'{}' is a double-sided image (819200 bytes).  Use "
            "--diskN (or -dN) to mount a whole double-sided drive "
            "from one image.",
            path);
    return std::format(
        "'{}' has unrecognised disk format (size {} bytes; expected 409600 "
        "for a single-side image).",
        path, static_cast<unsigned long long>(sz));
}

std::optional<std::string>
validateDoubleSidedImage(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return std::format("cannot stat '{}': {}", path, ec.message());
    if (sz == 819200)
        return std::nullopt;
    if (sz == 409600)
        return std::format(
            "'{}' is a single-side image (409600 bytes).  Use "
            "--diskN-side0 (or -dNs0) to mount it on one side of "
            "a drive.",
            path);
    return std::format(
        "'{}' has unrecognised disk format (size {} bytes; expected 819200 "
        "for a double-sided image).",
        path, static_cast<unsigned long long>(sz));
}

std::string saveScreenshot(const Video &video, const std::string &path)
{
    std::string outPath = path;
    if (outPath.empty()) {
        std::time_t t = std::time(nullptr);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "ms0515_%Y-%m-%d_%H%M%S.png", &tm);
        outPath = Paths::exeDir() + buf;
    }
    int rc = stbi_write_png(outPath.c_str(),
                            kScreenWidth,
                            kScreenHeight,
                            4,  /* RGBA */
                            video.pixels(),
                            kScreenWidth * 4);
    if (!rc)
        return {};
    std::fprintf(stderr, "Screenshot saved: %s\n", outPath.c_str());
    return outPath;
}

} /* namespace ms0515_frontend */
