#define _CRT_SECURE_NO_WARNINGS
#include "Assets.hpp"
#include "Config.hpp"
#include "Video.hpp"

#include <ms0515/floppy.h>     /* FDC_DISK_SIZE */

/* stb is header-only; we own the single .cpp that supplies the
 * implementation symbols. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>

namespace ms0515_frontend {

namespace {

/* Common shape of a disk-image size check: file must exist and be
 * exactly `expected` bytes.  When the size matches the *other* common
 * format we point the user at the right CLI flag. */
std::optional<std::string>
validateDiskImage(const std::string &path, std::uintmax_t expected)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return std::format("cannot stat '{}': {}", path, ec.message());
    if (sz == expected)
        return std::nullopt;

    const bool wantedDouble = (expected == 2 * FDC_DISK_SIZE);
    const bool sizeIsOther  = (sz == (wantedDouble ? FDC_DISK_SIZE
                                                   : 2 * FDC_DISK_SIZE));
    if (sizeIsOther) {
        return wantedDouble
            ? std::format(
                "'{}' is a single-side image ({} bytes).  Use "
                "--diskN-side0 (or -dNs0) to mount it on one side of "
                "a drive.", path, FDC_DISK_SIZE)
            : std::format(
                "'{}' is a double-sided image ({} bytes).  Use "
                "--diskN (or -dN) to mount a whole double-sided drive "
                "from one image.", path, 2 * FDC_DISK_SIZE);
    }
    return std::format(
        "'{}' has unrecognised disk format (size {} bytes; expected {} "
        "for a {} image).",
        path, static_cast<unsigned long long>(sz), expected,
        wantedDouble ? "double-sided" : "single-side");
}

} /* anonymous namespace */

std::string findDefaultRom()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    constexpr const char *kCandidate = "assets/rom/ms0515-roma.rom";
    for (const auto &root : Paths::searchRoots()) {
        fs::path candidate = root / kCandidate;
        if (fs::exists(candidate, ec))
            return candidate.lexically_normal().string();
    }
    return {};
}

std::vector<std::string> discoverRoms()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> result;
    for (const auto &root : Paths::searchRoots()) {
        fs::path romDir = root / "assets" / "rom";
        if (!fs::is_directory(romDir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(romDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".rom") continue;
            std::string p = entry.path().lexically_normal().string();
            if (std::find(result.begin(), result.end(), p) == result.end())
                result.push_back(p);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string>
validateSingleSideImage(const std::string &path)
{
    return validateDiskImage(path, FDC_DISK_SIZE);
}

std::optional<std::string>
validateDoubleSidedImage(const std::string &path)
{
    return validateDiskImage(path, 2 * FDC_DISK_SIZE);
}

std::string saveScreenshot(const Video &video, const std::string &path)
{
    std::string outPath = path.empty()
        ? Paths::timestamped("ms0515", ".png")
        : path;
    int rc = stbi_write_png(outPath.c_str(),
                            kScreenWidth, kScreenHeight,
                            4,  /* RGBA */
                            video.pixels(),
                            kScreenWidth * 4);
    if (!rc) return {};
    std::fprintf(stderr, "Screenshot saved: %s\n", outPath.c_str());
    return outPath;
}

} /* namespace ms0515_frontend */
