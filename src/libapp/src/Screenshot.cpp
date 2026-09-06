/*
 * Screenshot.cpp — PNG writer for the composed frame.
 *
 * stb_image_write is compiled with STBI_WRITE_NO_STDIO: its own file
 * helpers call fopen, which MSVC's /W4 /WX rejects as deprecated, and
 * silencing that with _CRT_SECURE_NO_WARNINGS is not allowed here.  The
 * *_to_func variant hands us the encoded bytes instead and we do the
 * writing ourselves through std::ofstream, which is portable and needs
 * no suppression.
 */

#include <ms0515/app/Screen.hpp>
#include <ms0515/app/Paths.hpp>     /* Paths::timestamped */

/* stb is header-only; we own the single .cpp that supplies the
 * implementation symbols. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace ms0515::app {

namespace {

/* stb calls this back with each chunk of the encoded stream. */
void collect(void *context, void *data, int size)
{
    auto *out = static_cast<std::vector<char> *>(context);
    const char *bytes = static_cast<const char *>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} /* anonymous namespace */

std::string saveScreenshot(const Screen &screen, const std::string &path)
{
    const std::string outPath = path.empty()
        ? Paths::timestamped("ms0515", ".png")
        : path;

    std::vector<char> png;
    const int rc = stbi_write_png_to_func(collect, &png,
                                          kScreenWidth, kScreenHeight,
                                          4,  /* RGBA */
                                          screen.pixels(),
                                          kScreenWidth * 4);
    if (!rc || png.empty()) return {};

    std::ofstream out(outPath, std::ios::binary);
    if (!out) return {};
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    if (!out) return {};
    out.close();
    if (!out) return {};

    std::fprintf(stderr, "Screenshot saved: %s\n", outPath.c_str());
    return outPath;
}

} /* namespace ms0515::app */
