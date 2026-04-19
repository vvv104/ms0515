/*
 * Platform_unix.cpp — Linux / macOS platform implementation.
 */

#include "Platform.hpp"

#include <SDL.h>

namespace ms0515_frontend {

void platformInit()
{
    /* No special init needed on Unix. */
}

std::string openFileDialog(SDL_Window * /*owner*/, const char * /*title*/)
{
    /* No native file dialog — disks can be mounted via CLI args. */
    return {};
}

std::string saveFileDialog(SDL_Window * /*owner*/, const char * /*title*/,
                           const char * /*defaultName*/)
{
    /* No native file dialog on Unix yet. */
    return {};
}

std::vector<std::string> systemFontCandidates()
{
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    };
#else /* Linux / FreeBSD */
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
#endif
}

std::vector<std::string> symbolFontCandidates()
{
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Apple Symbols.ttf",
    };
#else
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
    };
#endif
}

} /* namespace ms0515_frontend */
