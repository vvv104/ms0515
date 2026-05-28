/*
 * Assets.cpp — frontend-only screenshot saver.  ROM/disk discovery and
 * validation live in libapp/Disks.cpp now (shared with ms0515-cli).
 */

#define _CRT_SECURE_NO_WARNINGS
#include "Assets.hpp"
#include "Config.hpp"   /* Paths::timestamped */
#include "Video.hpp"

/* stb is header-only; we own the single .cpp that supplies the
 * implementation symbols. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <string>

namespace ms0515_frontend {

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
